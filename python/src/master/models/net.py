"""Residual policy with mandatory history attention and ordinary actions."""
# 编码切片和动作编号由原生 schema 决定；模型不能自行改变这些布局。
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import torch
from blacksmith import master as engine
from torch import Tensor, nn

ARCHITECTURE = "routed-profession-v1"


@dataclass(frozen=True)
class ModelConfig:
    hidden: int = 256
    features: int = engine.OBSERVATION_LENGTH
    actions: int = engine.BASE_ACTION_COUNT
    depth: int = 4
    history_width: int = 64
    history_layers: int = 1
    history_heads: int = 4

    def __post_init__(self) -> None:
        if self.hidden < 8 or self.depth < 1 or self.history_layers < 1:
            raise ValueError("Residual depth and history layer count must be positive; hidden width must be >= 8")
        if self.features != engine.OBSERVATION_LENGTH or self.actions != engine.BASE_ACTION_COUNT:
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
        indices = torch.where(valid.unsqueeze(-1),
                              history[..., 1:3].long() % engine.BASE_ACTION_COUNT,
                              self.padding_index)
        actions = self.actions(indices).flatten(-2)
        numeric = torch.where(valid.unsqueeze(-1), history[..., 3:], 0)
        tokens = actions + self.numeric(numeric) + self.position
        tokens = torch.cat((self.summary.expand(history.shape[0], -1, -1), tokens), dim=1)
        # summary 永远有效，空历史也有可读出的 token，避免整行注意力全部被屏蔽。
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
        # 保留恒等路径，并随总块数缩小每条残差分支，控制深层主干的更新尺度。
        return x + self.scale * self.branch(x)


class Policy(nn.Module):
    def __init__(self, config: ModelConfig | None = None, *, initialize: bool = True) -> None:
        super().__init__()
        self.config = config or ModelConfig()
        cfg = self.config
        # Share each field-family encoder between the two player perspectives.
        self.public = nn.Sequential(nn.Linear(engine.COMMON_ENCODING_LENGTH, cfg.hidden // 2), nn.SiLU())
        self.profession = nn.Sequential(nn.Linear(engine.PROFESSION_ENCODING_LENGTH, cfg.hidden // 4), nn.SiLU())
        self.history = HistoryEncoder(cfg)
        inputs = (2 * (cfg.hidden // 2 + cfg.hidden // 4)
                  + engine.HISTORY_OFFSET - engine.GLOBAL_OFFSET + cfg.history_width)
        self.body = nn.Sequential(nn.Linear(inputs, cfg.hidden), nn.SiLU(),
                                  *(ResidualBlock(cfg.hidden, cfg.depth) for _ in range(cfg.depth)),
                                  nn.LayerNorm(cfg.hidden))
        self.actor = nn.Linear(cfg.hidden, engine.BASE_ACTION_COUNT)
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
        # 展平任意批量前缀，最终还原；训练可一次输入“环境 × 双方视角”。
        flat = observation.reshape(-1, self.config.features)
        common, profession = engine.COMMON_ENCODING_LENGTH, engine.PROFESSION_ENCODING_LENGTH
        parts = [self.public(flat[:, :common]), self.public(flat[:, common:2 * common]),
                 self.profession(flat[:, 2 * common:2 * common + profession]),
                 self.profession(flat[:, 2 * common + profession:engine.GLOBAL_OFFSET]),
                 flat[:, engine.GLOBAL_OFFSET:engine.HISTORY_OFFSET]]
        past = flat[:, engine.HISTORY_OFFSET:].reshape(-1, engine.HISTORY_LENGTH, engine.HISTORY_TOKEN_LENGTH)
        parts.append(self.history(past))
        hidden = self.body(torch.cat(parts, dim=-1))
        logits = self.actor(hidden)
        logits = logits.reshape(*shape, self.config.actions)
        raw = self.critic(hidden).reshape(shape)
        # 训练取原始 logits 使用稳定的 BCE；推理默认输出 [0,1] 预期得分。
        values = raw if value_logits else raw.sigmoid()
        return logits, values

    def signed_values(self, values: Tensor) -> Tensor:
        """Convert expected score to the zero-sum utility used by search."""
        return 2 * values - 1


def masked_log_probs(logits: Tensor, masks: Tensor) -> Tensor:
    if logits.shape != masks.shape or not masks.bool().any(-1).all():
        raise ValueError("Every policy row needs at least one legal action")
    # 即使主干用 BF16，softmax 仍在 FP32 中计算，并排除所有原生判定的非法动作。
    return logits.float().masked_fill(~masks.bool(), -1e9).log_softmax(-1)


class ProfessionModel(nn.Module):
    """Zero-initialized correction to the frozen base policy and value logits."""
    def __init__(self, config: ModelConfig, phantom: bool) -> None:
        super().__init__()
        self.phantom = phantom
        self.body = nn.Sequential(nn.Linear(config.features + config.actions + 1, config.hidden), nn.SiLU())
        self.output = nn.Linear(config.hidden, config.actions + 1 + (config.actions if phantom else 0))
        nn.init.zeros_(self.output.weight)
        nn.init.zeros_(self.output.bias)

    def forward(self, observation: Tensor, logits: Tensor, value: Tensor) -> Tensor:
        return self.output(self.body(torch.cat((observation, logits, value.unsqueeze(-1)), dim=-1)))


class RoutedPolicy(nn.Module):
    def __init__(self, base: Policy, enabled_roles: tuple[str, ...] = ()) -> None:
        super().__init__()
        self.base = base
        self.config = base.config
        self.experts = nn.ModuleDict({role: ProfessionModel(self.config, role == "phantombook")
                                      for role in engine.ROLES[1:]})
        self.enabled_roles = tuple(enabled_roles)

    def forward(self, observation: Tensor, value_logits: bool = False) -> tuple[Tensor, Tensor]:
        shape = observation.shape[:-1]
        flat = observation.reshape(-1, self.config.features)
        base_input = flat.clone()
        association = engine.AUTHORIZED_OFFSET + engine.ASSOCIATION_SKILL
        base_input[:, association] = 0
        base_input[:, engine.COMMON_ENCODING_LENGTH + association] = 0
        base_logits, raw = self.base(base_input, value_logits=True)
        logits = torch.cat((base_logits, torch.full_like(base_logits, -1e9)), dim=-1)
        roles = flat[:, engine.PROFESSION_ONE_HOT_OFFSET:
                     engine.PROFESSION_ONE_HOT_OFFSET + len(engine.ROLES)].argmax(-1)
        for role in self.enabled_roles:
            indices = (roles == engine.ROLES.index(role)).nonzero(as_tuple=True)[0]
            if indices.numel() == 0:
                continue
            correction = self.experts[role](flat[indices], base_logits[indices], raw[indices])
            count = engine.BASE_ACTION_COUNT
            ordinary = base_logits[indices] + correction[:, :count]
            tail = (base_logits[indices] + correction[:, count + 1:]
                    if role == "phantombook" else torch.full_like(ordinary, -1e9))
            logits = logits.index_copy(0, indices, torch.cat((ordinary, tail), -1))
            raw = raw.index_copy(0, indices, raw[indices] + correction[:, count])
        return logits.reshape(*shape, engine.SKILL_NUM), (raw if value_logits else raw.sigmoid()).reshape(shape)

    def mask(self, observations, masks):
        masks = masks.copy()
        if "phantombook" not in self.enabled_roles:
            masks[..., engine.BASE_ACTION_COUNT:] = 0
        else:
            role = observations[..., engine.PROFESSION_ONE_HOT_OFFSET:
                                engine.PROFESSION_ONE_HOT_OFFSET + len(engine.ROLES)].argmax(-1)
            masks[..., engine.BASE_ACTION_COUNT:] *= (role == engine.ROLES.index("phantombook"))[..., None]
        return masks

    @staticmethod
    def signed_values(values: Tensor) -> Tensor:
        return 2 * values - 1

    def train_stage(self, role: str | None) -> None:
        if role is not None and role not in engine.ROLES[1:]:
            raise ValueError("Unknown profession")
        for parameter in self.parameters():
            parameter.requires_grad_(False)
        if role is None:
            self.enabled_roles = ()
            self.base.requires_grad_(True)
        else:
            if role not in self.enabled_roles:
                self.enabled_roles = (*self.enabled_roles, role)
            self.experts[role].requires_grad_(True)


def checkpoint(model: RoutedPolicy, **metadata: Any) -> dict[str, Any]:
    return {**metadata, "model": {name: tensor.detach().cpu() for name, tensor in model.state_dict().items()},
            "architecture": ARCHITECTURE, "model_config": asdict(model.config),
            "schema_version": engine.SCHEMA_VERSION, "enabled_roles": model.enabled_roles,
            "features": model.config.features, "actions": engine.SKILL_NUM}


def load(path: str | Path) -> RoutedPolicy:
    data: dict[str, Any] = torch.load(path, map_location="cpu", weights_only=True)
    if (data.get("schema_version") != engine.SCHEMA_VERSION or
            data.get("features") != engine.OBSERVATION_LENGTH or
            data.get("actions") != engine.SKILL_NUM or data.get("architecture") != ARCHITECTURE):
        raise ValueError(f"Checkpoint {path} has an incompatible architecture or schema")
    roles = tuple(data.get("enabled_roles", ()))
    if len(set(roles)) != len(roles) or any(role not in engine.ROLES[1:] for role in roles):
        raise ValueError("Checkpoint contains invalid profession routing")
    model = RoutedPolicy(Policy(config=ModelConfig(**data["model_config"]), initialize=False), roles)
    model.load_state_dict(data["model"])
    return model.eval()


def load_with_specialists(base_path: str | Path, specialist_path: str | Path) -> RoutedPolicy:
    """Load the base checkpoint and overlay the enabled expert weights from another checkpoint.

    Expert corrections are trained against a particular frozen base. Refuse to combine
    mismatched bases so play cannot silently route an expert over incompatible logits.
    """
    base = load(base_path)
    specialist = load(specialist_path)
    if base.config != specialist.config:
        raise ValueError("Base and specialist checkpoints use different model configurations")

    base_state = base.state_dict()
    specialist_state = specialist.state_dict()
    base_keys = [key for key in base_state if key.startswith("base.")]
    if not base_keys or any(not torch.equal(base_state[key], specialist_state[key]) for key in base_keys):
        raise ValueError("Specialist checkpoint was trained from a different base checkpoint")
    roles = specialist.enabled_roles
    if not roles:
        raise ValueError("Specialist checkpoint does not enable any profession experts")

    merged_state = dict(base_state)
    for role in roles:
        prefix = f"experts.{role}."
        keys = [key for key in specialist_state if key.startswith(prefix)]
        if not keys:
            raise ValueError(f"Specialist checkpoint has no weights for {role}")
        for key in keys:
            merged_state[key] = specialist_state[key]
    base.load_state_dict(merged_state)
    base.enabled_roles = tuple(dict.fromkeys((*base.enabled_roles, *roles)))
    return base.eval()
