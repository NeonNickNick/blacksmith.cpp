"""Depth-two simultaneous matrix search with regret matching+."""
# 与本地迭代加深搜索不同，本模块固定两回合，适合大批量自对弈采样。
import numpy as np
import torch
from blacksmith import master as engine

from master.models import Policy, masked_log_probs
from master.models.data import Floats, Ints

DISCOUNT = .997


def solve_matrix(payoffs: Floats, row_valid: Floats, column_valid: Floats,
                 iterations: int = 64) -> tuple[Floats, Floats]:
    # payoffs 为 [批量, 第一席候选, 第二席候选]，数值始终从第一席视角定义。
    if iterations <= 0 or not row_valid.any(-1).all() or not column_valid.any(-1).all():
        raise ValueError("Matrix solver needs iterations and valid row/column strategies")
    row_regret = np.zeros(payoffs.shape[:2], np.float32)
    column_regret = np.zeros((payoffs.shape[0], payoffs.shape[2]), np.float32)
    row_sum, column_sum = np.zeros_like(row_regret), np.zeros_like(column_regret)
    row_uniform = row_valid / row_valid.sum(-1, keepdims=True)
    column_uniform = column_valid / column_valid.sum(-1, keepdims=True)
    for iteration in range(iterations):
        row_total = row_regret.sum(-1, keepdims=True)
        column_total = column_regret.sum(-1, keepdims=True)
        row = np.where(row_total > 1e-8, row_regret / np.maximum(row_total, 1e-8), row_uniform)
        column = np.where(column_total > 1e-8, column_regret / np.maximum(column_total, 1e-8), column_uniform)
        row_value = np.einsum("bij,bj->bi", payoffs, column)
        # 第二席最大化相反收益；累计正遗憾并投影到非负数，即 regret matching+。
        column_value = -np.einsum("bi,bij->bj", row, payoffs)
        value = (row * row_value).sum(-1, keepdims=True)
        row_regret = np.maximum(row_regret + row_value - value, 0) * row_valid
        column_regret = np.maximum(column_regret + column_value + value, 0) * column_valid
        row_sum += (iteration + 1) * row
        # 后期策略赋予更高平均权重，返回的是近似混合策略，而不是单个最优动作。
        column_sum += (iteration + 1) * column
    return row_sum / row_sum.sum(-1, keepdims=True), column_sum / column_sum.sum(-1, keepdims=True)


def matrix(payoffs: Floats, choices: Ints, iterations: int) -> tuple[Floats, Floats, Floats]:
    valid = (choices >= 0).astype(np.float32)
    for seat in range(2):
        empty = ~valid[:, seat].any(-1)
        # 终局/填充分支可能没有候选，临时保留一个占位项以避免归一化除零。
        valid[empty, seat, 0] = 1
    first, second = solve_matrix(payoffs, valid[:, 0], valid[:, 1], iterations)
    return first, second, np.einsum("bi,bij,bj->b", first, payoffs, second)


def terminals(payoffs: Floats, outcomes: Floats) -> None:
    # 真正终局覆盖网络估值：1/2为双方获胜，3/4为和局，-1为填充分支。
    payoffs[outcomes == 1] = 1
    payoffs[outcomes == 2] = -1
    payoffs[(outcomes == 3) | (outcomes == 4) | (outcomes == -1)] = 0


def sample_candidates(priors: Floats, masks: Floats, width: int,
                      rng: np.random.Generator, exploration: float = 0) -> Ints:
    if not 1 <= width <= priors.shape[-1] or not 0 <= exploration <= 1:
        raise ValueError("Invalid search width or exploration")
    legal = masks > 0
    counts = legal.sum(-1, keepdims=True)
    probabilities = (1 - exploration) * priors + exploration * legal / np.maximum(counts, 1)
    # Gumbel top-k 对合法动作无放回抽样；候选不足 width 时用 -1 补齐。
    keys = np.log(np.maximum(probabilities, 1e-30)) + rng.gumbel(size=priors.shape)
    keys[~legal] = -np.inf
    selected = np.argpartition(-keys, width - 1, axis=-1)[..., :width]
    scores = np.take_along_axis(keys, selected, axis=-1)
    selected = np.take_along_axis(selected, np.argsort(-scores, axis=-1), axis=-1)
    ordered = np.sort(np.where(legal, np.arange(priors.shape[-1]), priors.shape[-1]), axis=-1)[..., :width]
    selected = np.where(counts <= width, ordered, selected)
    selected[selected == priors.shape[-1]] = -1
    return np.ascontiguousarray(selected, dtype=np.int32)


def candidates(model: Policy, observations: Floats, masks: Floats, width: int,
               rng: np.random.Generator, device: torch.device, exploration: float) -> Ints:
    flat = observations.reshape(-1, engine.OBSERVATION_LENGTH)
    flat_masks = masks.reshape(-1, engine.SKILL_NUM)
    active = np.flatnonzero(flat_masks.any(-1))
    priors = np.zeros_like(flat_masks)
    for start in range(0, len(active), 8192):
        # 只给仍有合法动作的视角做网络前向，并限制单批推理的内存开销。
        indices = active[start:start + 8192]
        logits, _ = model(torch.as_tensor(flat[indices], device=device))
        mask = torch.as_tensor(flat_masks[indices], device=device)
        priors[indices] = masked_log_probs(logits, mask).exp().cpu().numpy()
    return sample_candidates(priors.reshape(masks.shape), masks, width, rng, exploration)


@torch.inference_mode()
def search(model: Policy, arena: engine.Arena, rng: np.random.Generator,
           device: torch.device, width: int = 8, leaf_width: int = 4,
           iterations: int = 64, exploration: float = 0,
           observation: tuple[Floats, Floats] | None = None) -> tuple[Ints, Floats]:
    if iterations <= 0:
        raise ValueError("Regret iterations must be positive")
    observations, masks = observation if observation is not None else arena.observe()
    tree = engine.SearchTree(arena)
    # 每次 expand 同时组合双方候选；搜索树复制状态，不能推进真实自对弈 Arena。
    root_choices = candidates(model, observations, masks, width, rng, device, exploration)
    middle, middle_masks, root_outcomes = tree.expand(root_choices, True)
    leaf_choices = candidates(model, middle, middle_masks, leaf_width, rng, device, exploration)
    leaves, _, leaf_outcomes = tree.expand(leaf_choices, False)
    active = np.flatnonzero(leaf_outcomes.reshape(-1) == 0)
    values = np.zeros((len(leaves), 2), np.float32)
    for start in range(0, len(active), 4096):
        indices = active[start:start + 4096]
        states = leaves[indices].reshape(-1, engine.OBSERVATION_LENGTH)
        _, value = model(torch.as_tensor(states, device=device))
        values[indices] = model.signed_values(value).float().cpu().numpy().reshape(-1, 2)
    values = values.reshape(-1, leaf_width, leaf_width, 2)
    # 双方各自视角的网络估值可能不完全反对称，取差的一半构造零和矩阵。
    payoffs = DISCOUNT * .5 * (values[..., 0] - values[..., 1])
    terminals(payoffs, leaf_outcomes)
    _, _, continuation = matrix(payoffs, leaf_choices, iterations)
    payoffs = DISCOUNT * continuation.reshape(arena.count, width, width)
    terminals(payoffs, root_outcomes)
    first, second, _ = matrix(payoffs, root_choices, iterations)
    # 监督目标只来自根部搜索策略，未进入候选集的动作保持零权重。
    policies = np.zeros((arena.count, 2, engine.SKILL_NUM), np.float32)
    for game in range(arena.count):
        for seat, distribution in enumerate((first[game], second[game])):
            valid = root_choices[game, seat] >= 0
            policies[game, seat, root_choices[game, seat, valid]] = distribution[valid]
    play = policies.astype(np.float64)
    if exploration:
        # 行为动作另混入少量合法均匀探索，但 replay 仍保存上面的搜索策略标签。
        play = .97 * play + .03 * masks / masks.sum(-1, keepdims=True)
    play /= play.sum(-1, keepdims=True)
    uniforms = rng.random(play.shape[:2])[..., None]
    cumulative = play.cumsum(-1)
    cumulative[..., -1] = 1.0
    actions = (uniforms > cumulative).sum(-1).astype(np.int32)
    return actions, policies
