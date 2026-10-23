"""A frozen master network with simultaneous matrix search."""
# Agent 只做推理：模型固定，落子与复盘分析使用相互独立的随机数流。
from pathlib import Path

import numpy as np
import torch
from blacksmith import master as engine

from .models import load
from .train.search import search


class Agent:
    def __init__(self, checkpoint: str | Path, device: str = "cpu", seed: int = 42,
                 width: int = 24, leaf_width: int = 12, iterations: int = 128,
                 search_depth: int | None = None, max_nodes: int = 200000,
                 batch_cells: int = 4096) -> None:
        self.device = torch.device(device)
        self.model = load(checkpoint).to(self.device).eval()
        self.rng = np.random.default_rng(seed)
        # 分析按同一种子重新建 RNG，查看推荐不会改变下一次正式落子的抽样。
        self.analysis_seed = seed
        self.width, self.leaf_width, self.iterations = width, leaf_width, iterations
        self.search_depth, self.max_nodes, self.batch_cells = search_depth, max_nodes, batch_cells
        self.last_search = None

    @torch.inference_mode()
    def predict_score(self, arena: engine.Arena, seat: int = 0) -> float:
        """Value-head expected score from this seat: win=1, draw=0.5, loss=0.

        Includes the training time discount; this is not a calibrated pure
        probability of winning, nor the search matrix's estimated value.
        """
        if arena.count != 1 or seat not in (0, 1):
            raise ValueError('Agent.predict_score expects one game and seat 0 or 1')
        observations, _ = arena.observe()
        # 原生 observe 已经提供两个视角，seat 决定“自己”的资源和历史方向。
        state = torch.as_tensor(observations[0, seat], device=self.device).unsqueeze(0)
        _, values = self.model(state)
        score = float(values.item())
        if not np.isfinite(score) or not 0 <= score <= 1:
            raise ValueError('Invalid value-head prediction')
        return score

    def analyze(self, arena: engine.Arena):
        """Search both sides for review, independently of live action sampling."""
        if arena.count != 1 or arena.status(0)['outcome']:
            raise ValueError('Analysis requires one ongoing game')
        from .search import search as local_search
        # 至少保留三个根候选以支持前三推荐；合法动作少于三个时仍只列真实候选。
        _, policies, info = local_search(self.model, arena,
            np.random.default_rng(self.analysis_seed), self.device,
            max(3, self.width), self.leaf_width, self.iterations,
            self.search_depth or 2, self.max_nodes, self.batch_cells)
        return policies[0], info

    def choose(self, arena: engine.Arena, seat: int = 0) -> int:
        if arena.count != 1 or seat not in (0, 1):
            raise ValueError("Agent.choose expects one game and seat 0 or 1")
        # 未指定本地深度时复用训练版两回合搜索；play 命令通常显式提供深度。
        if self.search_depth is None:
            actions, _ = search(self.model, arena, self.rng, self.device,
                                self.width, self.leaf_width, self.iterations)
        else:
            from .search import search as local_search
            actions, _, self.last_search = local_search(self.model, arena, self.rng, self.device,
                self.width, self.leaf_width, self.iterations, self.search_depth,
                self.max_nodes, self.batch_cells)
        return int(actions[0, seat])
