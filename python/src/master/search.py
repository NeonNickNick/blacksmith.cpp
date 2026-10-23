"""Bounded, iterative deepening for local simultaneous-action play."""
# 本地搜索复用训练版的候选生成与矩阵求解，但可在预算内递归更多完整回合。
from dataclasses import dataclass

import numpy as np
import torch
from blacksmith import master as engine

from .train.search import DISCOUNT, candidates, matrix, terminals


@dataclass
class SearchInfo:
    # completed_depth 只记录完整求解的深度；不能把中途用尽预算的一层算作完成。
    requested_depth: int
    completed_depth: int = 0
    expanded_nodes: int = 0
    budget_exhausted: bool = False
    root_candidates: np.ndarray | None = None


class NodeBudgetExceeded(RuntimeError):
    # 仅用于中止当前加深层；外层保留前一层的完整策略作为回退。
    pass


@torch.inference_mode()
def search(model, arena, rng, device, width=24, leaf_width=12, iterations=128,
           depth=2, max_nodes=200000, batch_cells=4096):
    if (min(width, leaf_width, iterations, depth, max_nodes, batch_cells) <= 0
            or max(width, leaf_width) > engine.SKILL_NUM or depth > engine.ROUND_LIMIT):
        raise ValueError(f'Positive search limits required; widths <={engine.SKILL_NUM}, depth <={engine.ROUND_LIMIT}')
    observation, masks = arena.observe()
    root_width = min(width, int(masks.sum(-1).max()))
    root_choices = candidates(model, observation, masks, root_width, rng, device, 0)
    # 各加深层复用同一批根候选，避免候选抽样变化干扰深度比较和复盘排序。
    info = SearchInfo(depth, root_candidates=root_choices.copy())

    def evaluate(tree, states, legal, remaining, choices=None, root=False):
        if choices is None:
            branch_width = min(leaf_width, int(legal.sum(-1).max()))
            choices = candidates(model, states, legal, branch_width, rng, device, 0)
        branch_width = choices.shape[-1]
        count = len(states)
        values = np.empty(count, np.float32)
        policies = np.zeros((count, 2, engine.SKILL_NUM), np.float32) if root else None
        batch = max(1, batch_cells // (branch_width * branch_width))
        for start in range(0, count, batch):
            stop = min(start + batch, count)
            cells = (stop - start) * branch_width * branch_width
            # 一个联合节点是一对同时行动；分支数按 width² 增长，而不是轮流交替。
            if info.expanded_nodes + cells > max_nodes:
                raise NodeBudgetExceeded()
            info.expanded_nodes += cells
            subtree = tree.subset(list(range(start, stop)))
            child_states, child_masks, outcomes = subtree.expand(
                np.ascontiguousarray(choices[start:stop]), remaining > 1)
            live = np.flatnonzero(outcomes.reshape(-1) == 0)
            payoffs = np.zeros(cells, np.float32)
            if len(live):
                # 只递归尚未终局的分支；叶子使用双方视角价值差构造零和收益。
                if remaining > 1:
                    child_values, _ = evaluate(subtree.subset(live.tolist()),
                        child_states[live], child_masks[live], remaining - 1)
                else:
                    child_values = np.empty(len(live), np.float32)
                    for offset in range(0, len(live), 4096):
                        indices = live[offset:offset + 4096]
                        _, predicted = model(torch.as_tensor(
                            child_states[indices].reshape(-1, engine.OBSERVATION_LENGTH), device=device))
                        signed = model.signed_values(predicted).float().cpu().numpy().reshape(-1, 2)
                        child_values[offset:offset + len(indices)] = .5 * (signed[:, 0] - signed[:, 1])
                payoffs[live] = DISCOUNT * child_values
            payoffs = payoffs.reshape(stop - start, branch_width, branch_width)
            terminals(payoffs, outcomes)
            first, second, values[start:stop] = matrix(payoffs, choices[start:stop], iterations)
            if root:
                for game in range(stop - start):
                    for seat, distribution in enumerate((first[game], second[game])):
                        valid = choices[start + game, seat] >= 0
                        policies[start + game, seat, choices[start + game, seat, valid]] = distribution[valid]
        return values, policies

    best = None
    for current_depth in range(1, depth + 1):
        # 节点预算累计覆盖所有加深层，抛出异常时绝不使用未完成层的局部结果。
        try:
            _, policy = evaluate(engine.SearchTree(arena), observation, masks,
                                 current_depth, root_choices, root=True)
        except NodeBudgetExceeded:
            info.budget_exhausted = True
            break
        best = policy
        info.completed_depth = current_depth
    if best is None:
        raise ValueError(f'--search-nodes must be at least {arena.count * root_width ** 2} for this root')
    play = best.astype(np.float64)
    # 从求得的混合策略抽样；直接 argmax 会丢掉同时行动博弈需要的随机性。
    play /= play.sum(-1, keepdims=True)
    cumulative = play.cumsum(-1)
    cumulative[..., -1] = 1
    actions = (rng.random(play.shape[:2])[..., None] > cumulative).sum(-1).astype(np.int32)
    return actions, best, info
