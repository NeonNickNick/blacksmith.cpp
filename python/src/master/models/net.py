"""Residual policy with mandatory history attention and association actions."""
# 编码切片和动作编号由原生 schema 决定；模型不能自行改变这些布局。
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

import torch
from blacksmith import master as engine
from torch import Tensor, nn

ARCHITECTURE = "residual-history-association-v1"


@dataclass(frozen=True)
class ModelConfig:
    hidden: int = 256
    features: int = engine.OBSERVATION_LENGTH
    actions: int = engine.SKILL_NUM
    depth: int = 4
    history_width: int = 64
    history_layers: int = 1
    history_heads: int = 4

    def __post_init__(self) -> None:
        # 联想动作与普通动作一一配对；维度校验防止旧权重静默套用新规则。
        if self.hidden < 8 or self.depth < 1 or self.history_layers < 1:
            raise ValueError("Residual depth and history layer count must be positive; hidden width must be >= 8")
        if self.actions != 2 * engine.BASE_ACTION_COUNT:
            raise ValueError("Association policy requires paired ordinary/borrowed action tables")
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
        self.base_actions = nn.Embedding(engine.BASE_ACTION_COUNT + 1, width // 2,
                                         padding_idx=engine.BASE_ACTION_COUNT)
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
        base = torch.where(valid.unsqueeze(-1), indices % engine.BASE_ACTION_COUNT,
                           engine.BASE_ACTION_COUNT)
        # 完整动作嵌入区分是否联想；基础动作嵌入让两种用法共享技能信息。
        actions = actions + self.base_actions(base).flatten(-2)
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
        self.association_actor = nn.Linear(cfg.hidden, engine.BASE_ACTION_COUNT)
        self.association_gate = nn.Linear(cfg.hidden, 1)
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
            # Start from shared skill scores; learn borrowing's cost and context.
            nn.init.zeros_(self.association_actor.weight)
            nn.init.zeros_(self.association_actor.bias)
            nn.init.zeros_(self.association_gate.weight)
            nn.init.zeros_(self.association_gate.bias)
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
        # 前半为普通动作 b，后半为 b+r+g；合法性归一化在完整动作空间统一执行。
        borrowed = logits + self.association_actor(hidden) + self.association_gate(hidden)
        logits = torch.cat((logits, borrowed), dim=-1)
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


def checkpoint(model: Policy, **metadata: Any) -> dict[str, Any]:
    # 模型参数转到 CPU 保存；训练入口另外传入 Adam 状态与累计更新计数。
    return {**metadata, "model": {name: tensor.detach().cpu() for name, tensor in model.state_dict().items()},
            "architecture": ARCHITECTURE, "model_config": asdict(model.config),
            "schema_version": engine.SCHEMA_VERSION,
            "features": model.config.features, "actions": model.config.actions}


def load(path: str | Path) -> Policy:
    # 先核对规则 schema 与架构标记，再按保存的配置建网；不猜测旧模型格式。
    data: dict[str, Any] = torch.load(path, map_location="cpu", weights_only=True)
    if (data.get("schema_version") != engine.SCHEMA_VERSION or
            data.get("features") != engine.OBSERVATION_LENGTH or
            data.get("actions") != engine.SKILL_NUM):
        raise ValueError(
            f"Checkpoint {path} uses schema/features/actions "
            f"{data.get('schema_version')}/{data.get('features')}/{data.get('actions')}; "
            f"native expects {engine.SCHEMA_VERSION}/{engine.OBSERVATION_LENGTH}/{engine.SKILL_NUM}. "
            "Retrain for the current rules; old weights cannot be loaded as a new policy.")
    if data.get("architecture") != ARCHITECTURE:
        raise ValueError(f"Checkpoint {path} does not use {ARCHITECTURE}; retrain with the current architecture.")
    model = Policy(config=ModelConfig(**data["model_config"]), initialize=False)
    model.load_state_dict(data["model"])
    return model.eval()
