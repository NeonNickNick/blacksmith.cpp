"""A frozen master network with simultaneous matrix search."""
from pathlib import Path

import numpy as np
import torch
from blacksmith import master as engine

from .models import load
from .train.search import search


class Agent:
    def __init__(self, checkpoint: str | Path, device: str = "cpu", seed: int = 42,
                 width: int = 24, leaf_width: int = 12, iterations: int = 128) -> None:
        self.device = torch.device(device)
        self.model = load(checkpoint).to(self.device).eval()
        self.rng = np.random.default_rng(seed)
        self.width, self.leaf_width, self.iterations = width, leaf_width, iterations

    def choose(self, arena: engine.Arena, seat: int = 0) -> int:
        if arena.count != 1 or seat not in (0, 1):
            raise ValueError("Agent.choose expects one game and seat 0 or 1")
        actions, _ = search(self.model, arena, self.rng, self.device,
                            self.width, self.leaf_width, self.iterations)
        return int(actions[0, seat])
