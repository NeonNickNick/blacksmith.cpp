"""Shared state encoders, optional history, and a configurable residual trunk."""
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import torch
from blacksmith import master as engine
from torch import Tensor, nn


@dataclass(frozen=True)
class ModelConfig:
    hidden: int = 256
    features: int = engine.OBSERVATION_LENGTH
    actions: int = engine.SKILL_NUM
    depth: int = 3
    history_width: int = 64
    history_layers: int = 2
    history_heads: int = 4
    backbone: str = "mlp"
    value_head: str = "tanh"

    def __post_init__(self) -> None:
        if self.hidden < 8 or self.depth < 1 or self.history_layers < 0:
            raise ValueError("Invalid MLP dimensions or history layer count")
        if self.backbone not in ("mlp", "residual") or self.value_head not in ("tanh", "sigmoid"):
            raise ValueError("Unknown backbone or value head")
        if self.features != engine.OBSERVATION_LENGTH or self.actions != engine.SKILL_NUM:
            raise ValueError("Model dimensions must match the native schema")
        if (self.history_width < 2 or self.history_width % 2 or self.history_heads < 1
                or self.history_width % self.history_heads):
            raise ValueError("History width must be even and divisible by its head count")


class HistoryEncoder(nn.Module):
    def __init__(self, config: ModelConfig) -> None:
        super().__init__()
        width = config.history_width
        self.padding_index = config.actions
        self.actions = nn.Embedding(config.actions + 1, width // 2, padding_idx=config.actions)
        self.numeric = nn.Linear(engine.HISTORY_TOKEN_LENGTH - 3, width)
        self.position = nn.Parameter(torch.empty(1, engine.HISTORY_LENGTH, width))
        self.summary = nn.Parameter(torch.empty(1, 1, width))
        layer = nn.TransformerEncoderLayer(width, config.history_heads, width * 2,
                                           dropout=0, batch_first=True, norm_first=True)
        self.encoder = nn.TransformerEncoder(layer, config.history_layers,
                                             norm=nn.LayerNorm(width), enable_nested_tensor=False)
        nn.init.normal_(self.position, std=.02)
        nn.init.normal_(self.summary, std=.02)

    def forward(self, history: Tensor) -> Tensor:
        valid = history[..., 0] > 0
        # Replace padding before embedding/projection, so unused data has no effect.
        indices = torch.where(valid.unsqueeze(-1), history[..., 1:3].long(), self.padding_index)
        actions = self.actions(indices).flatten(-2)
        numeric = torch.where(valid.unsqueeze(-1), history[..., 3:], 0)
        tokens = actions + self.numeric(numeric) + self.position
        tokens = torch.cat((self.summary.expand(history.shape[0], -1, -1), tokens), dim=1)
        padding = torch.cat((torch.zeros_like(valid[:, :1]), ~valid), dim=1)
        return self.encoder(tokens, src_key_padding_mask=padding)[:, 0]


class ResidualBlock(nn.Module):
    """Pre-normalized residual branch, scaled across the whole trunk."""
    def __init__(self, width: int, blocks: int) -> None:
        super().__init__()
        self.branch = nn.Sequential(nn.LayerNorm(width), nn.Linear(width, width),
                                    nn.SiLU(), nn.Linear(width, width))
        self.scale = blocks ** -.5

    def forward(self, x: Tensor) -> Tensor:
        return x + self.scale * self.branch(x)


class Policy(nn.Module):
    def __init__(self, hidden: int = 256, initialize: bool = True,
                 config: ModelConfig | None = None) -> None:
        super().__init__()
        self.config = config or ModelConfig(hidden=hidden)
        cfg = self.config
        # Share each field-family encoder between the two player perspectives.
        activation = nn.SiLU if cfg.backbone == "residual" else nn.Tanh
        self.public = nn.Sequential(nn.Linear(engine.COMMON_ENCODING_LENGTH, cfg.hidden // 2), activation())
        self.profession = nn.Sequential(nn.Linear(engine.PROFESSION_ENCODING_LENGTH, cfg.hidden // 4), activation())
        self.history = HistoryEncoder(cfg) if cfg.history_layers else None
        inputs = 2 * (cfg.hidden // 2 + cfg.hidden // 4) + 2
        if self.history is not None:
            inputs += cfg.history_width
        layers: list[nn.Module] = []
        if cfg.backbone == "residual":
            layers.extend((nn.Linear(inputs, cfg.hidden), nn.SiLU()))
            layers.extend(ResidualBlock(cfg.hidden, cfg.depth) for _ in range(cfg.depth))
            layers.append(nn.LayerNorm(cfg.hidden))
        else:
            for index in range(cfg.depth):
                layers.extend((nn.Linear(inputs if index == 0 else cfg.hidden, cfg.hidden), nn.Tanh()))
        self.body = nn.Sequential(*layers)
        self.actor = nn.Linear(cfg.hidden, cfg.actions)
        self.critic = nn.Linear(cfg.hidden, 1)
        if initialize:
            for layer in self.modules():
                if isinstance(layer, nn.Linear):
                    nn.init.orthogonal_(layer.weight, 2 ** .5)
                    nn.init.zeros_(layer.bias)
                elif isinstance(layer, nn.MultiheadAttention):
                    assert layer.in_proj_weight is not None
                    nn.init.xavier_uniform_(layer.in_proj_weight)
                    if layer.in_proj_bias is not None:
                        nn.init.zeros_(layer.in_proj_bias)
            nn.init.orthogonal_(self.actor.weight, .01)
            nn.init.zeros_(self.critic.weight)

    def forward(self, observation: Tensor, value_logits: bool = False) -> tuple[Tensor, Tensor]:
        if observation.shape[-1] != self.config.features:
            raise ValueError("Observation dimensions do not match the model")
        shape = observation.shape[:-1]
        flat = observation.reshape(-1, self.config.features)
        common, profession = engine.COMMON_ENCODING_LENGTH, engine.PROFESSION_ENCODING_LENGTH
        parts = [self.public(flat[:, :common]), self.public(flat[:, common:2 * common]),
                 self.profession(flat[:, 2 * common:2 * common + profession]),
                 self.profession(flat[:, 2 * common + profession:engine.GLOBAL_OFFSET]),
                 flat[:, engine.GLOBAL_OFFSET:engine.HISTORY_OFFSET]]
        if self.history is not None:
            past = flat[:, engine.HISTORY_OFFSET:].reshape(-1, engine.HISTORY_LENGTH, engine.HISTORY_TOKEN_LENGTH)
            parts.append(self.history(past))
        hidden = self.body(torch.cat(parts, dim=-1))
        logits = self.actor(hidden).reshape(*shape, self.config.actions)
        raw = self.critic(hidden).reshape(shape)
        values = raw if value_logits else (raw.sigmoid() if self.config.value_head == "sigmoid" else raw.tanh())
        return logits, values

    def signed_values(self, values: Tensor) -> Tensor:
        """Convert expected score to the zero-sum utility used by search."""
        return 2 * values - 1 if self.config.value_head == "sigmoid" else values


def masked_log_probs(logits: Tensor, masks: Tensor) -> Tensor:
    if logits.shape != masks.shape or not masks.bool().any(-1).all():
        raise ValueError("Every policy row needs at least one legal action")
    return logits.float().masked_fill(~masks.bool(), -1e9).log_softmax(-1)


def checkpoint(model: Policy, **metadata: Any) -> dict[str, Any]:
    return {**metadata, "model": {name: tensor.detach().cpu() for name, tensor in model.state_dict().items()},
            "model_config": asdict(model.config), "schema_version": engine.SCHEMA_VERSION,
            "features": model.config.features, "actions": model.config.actions,
            "hidden": model.config.hidden}


def load(path: str | Path) -> Policy:
    data: dict[str, Any] = torch.load(path, map_location="cpu", weights_only=True)
    if (data.get("schema_version") != engine.SCHEMA_VERSION or
            data.get("features") != engine.OBSERVATION_LENGTH or
            data.get("actions") != engine.SKILL_NUM):
        raise ValueError("Checkpoint is incompatible with the master observation/action schema")
    model = Policy(config=ModelConfig(**data["model_config"]), initialize=False)
    model.load_state_dict(data["model"])
    return model.eval()
