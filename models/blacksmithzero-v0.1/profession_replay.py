"""Separate sampling strata, ordinary search-policy and terminal-outcome targets."""
import json
from pathlib import Path
import numpy as np
from training_core import Replay


class Starts:
    def __init__(self, mode, bank, seed):
        self.mode = mode
        self.rng = np.random.default_rng(seed)
        self.started = self.courses = 0
        self.entries = json.loads(Path(bank).read_text())['entries'] if mode == 'mixed' else []
        self.pairs = {}
        for entry in self.entries:
            self.pairs.setdefault(tuple(entry['roles']), []).append(entry)

    def next(self):
        # Every ten started episodes: seven normal starts, three curriculum starts.
        course = self.mode == 'mixed' and self.started % 10 >= 7
        self.started += 1
        if not course:
            return -1, None
        role = self.courses % 6
        opponent = self.courses // 6 % 6
        seat = self.courses // 36 % 2
        pair = (role, opponent) if seat == 0 else (opponent, role)
        candidates = self.pairs[pair]
        entry = candidates[int(self.rng.integers(len(candidates)))]
        self.courses += 1
        return role, entry


class StratifiedReplay:
    def __init__(self, mode, capacity):
        self.mode = mode
        sizes = {-1: capacity} if mode == 'free' else {-1: int(capacity*.7), **{i: int(capacity*.05) for i in range(6)}}
        self.pools = {key: Replay(size) for key, size in sizes.items()}

    def add(self, group, history, outcome):
        self.pools[group].add_game(history, outcome)

    def ready(self, batch_size):
        return all(pool.size >= 64 for pool in self.pools.values()) and sum(p.size for p in self.pools.values()) >= batch_size

    def sample(self, rng, size):
        counts = {-1: size}
        if self.mode == 'mixed':
            free = round(size*.7)
            counts = {-1: free, **{i: (size-free)//6 + (i < (size-free)%6) for i in range(6)}}
        arrays = [[], [], [], [], []]
        for group, count in counts.items():
            pool = self.pools[group]
            indices = rng.integers(pool.size, size=count)
            for out, source in zip(arrays[:4], [pool.states,pool.masks,pool.policies,pool.values]):
                out.append(source[indices])
            mass = 1.0 if self.mode == 'free' else .7 if group == -1 else .05
            arrays[4].append(np.full(count, mass/count, np.float32))
        return [np.concatenate(parts) for parts in arrays]

    def sizes(self):
        return {str(key): pool.size for key,pool in self.pools.items()}
