"""Terminal-outcome replay with balanced 40/60 curriculum sampling."""
import json
from pathlib import Path

import numpy as np

from blacksmith import master as engine
from master.models.data import Floats, History, Prefix
from .search import DISCOUNT
from .sampling import COURSE_PAIRS, FREE_FRACTION, MIN_MIXED_BATCH, MIN_MIXED_REPLAY, masses


class Replay:
    def __init__(self, capacity: int) -> None:
        if capacity <= 0:
            raise ValueError("Replay capacity must be positive")
        self.states = np.empty((capacity, engine.OBSERVATION_LENGTH), np.float32)
        self.masks = np.empty((capacity, engine.SKILL_NUM), np.float32)
        self.policies = np.empty_like(self.masks)
        self.values = np.empty(capacity, np.float32)
        self.capacity, self.cursor, self.size = capacity, 0, 0

    def add_game(self, history: History, outcome: int) -> None:
        value = 1 if outcome == 1 else -1 if outcome == 2 else 0
        for turn, (states, masks, policies) in enumerate(history):
            for seat in range(2):
                index = self.cursor
                self.states[index], self.masks[index] = states[seat], masks[seat]
                self.policies[index] = policies[seat]
                self.values[index] = value * (1 if seat == 0 else -1) * DISCOUNT ** (len(history) - turn - 1)
                self.cursor = (self.cursor + 1) % self.capacity
                self.size = min(self.capacity, self.size + 1)


class StratifiedReplay:
    def __init__(self, mode: str, capacity: int) -> None:
        self.mode = mode
        self.masses = masses(mode)
        if mode == "mixed" and capacity < MIN_MIXED_REPLAY:
            raise ValueError(f"Mixed replay requires capacity >= {MIN_MIXED_REPLAY}")
        self.pools = {key: Replay(int(capacity * mass)) for key, mass in self.masses.items()}

    def add(self, group: int, history: History, outcome: int) -> None:
        self.pools[group].add_game(history, outcome)

    def ready(self, size: int) -> bool:
        return all(pool.size >= 64 for pool in self.pools.values()) and sum(
            pool.size for pool in self.pools.values()) >= size

    def sample(self, rng: np.random.Generator, size: int) -> tuple[Floats, Floats, Floats, Floats, Floats]:
        if self.mode == "mixed" and size < MIN_MIXED_BATCH:
            raise ValueError(f"Mixed batches require at least {MIN_MIXED_BATCH} samples")
        free = round(size * FREE_FRACTION)
        counts = {-1: size} if self.mode == "free" else {
            -1: free, **{i: (size - free) // len(COURSE_PAIRS) +
                        (i < (size - free) % len(COURSE_PAIRS)) for i in range(len(COURSE_PAIRS))}}
        arrays: list[list[Floats]] = [[], [], [], [], []]
        for group, count in counts.items():
            pool = self.pools[group]
            indices = rng.integers(pool.size, size=count)
            for output, source in zip(arrays[:4], (pool.states, pool.masks, pool.policies, pool.values)):
                output.append(source[indices])
            arrays[4].append(np.full(count, self.masses[group] / count, np.float32))
        result = [np.concatenate(parts) for parts in arrays]
        return result[0], result[1], result[2], result[3], result[4]

    def sizes(self) -> dict[str, int]:
        return {str(key): pool.size for key, pool in self.pools.items()}


class Starts:
    def __init__(self, mode: str, bank: str | None, seed: int) -> None:
        self.mode = mode
        self.rng = np.random.default_rng(seed)
        self.started = self.courses = 0
        self.order = np.arange(len(COURSE_PAIRS))
        self.pairs: dict[tuple[int, int], list[Prefix]] = {}
        if mode == "mixed":
            if bank is None:
                raise ValueError("Mixed self-play requires a curriculum bank")
            payload = json.loads(Path(bank).read_text(encoding="utf-8"))
            if payload["schema_version"] != engine.SCHEMA_VERSION:
                raise ValueError("Curriculum action schema mismatch")
            if payload.get("roles") != engine.ROLES:
                raise ValueError("Curriculum profession schema mismatch")
            for entry in payload["entries"]:
                first, second = entry["roles"]
                if (first, second) not in COURSE_PAIRS:
                    raise ValueError("Curriculum entries must use the five professions")
                self.pairs.setdefault((first, second), []).append(entry["prefix"])
            if any(pair not in self.pairs for pair in COURSE_PAIRS):
                raise ValueError("Curriculum bank must cover all 25 ordered profession pairs")

    def next(self) -> tuple[int, Prefix]:
        course = self.mode == "mixed" and self.started % 10 >= round(10 * FREE_FRACTION)
        self.started += 1
        if not course:
            return -1, []
        if self.courses % len(COURSE_PAIRS) == 0:
            self.rng.shuffle(self.order)
        group = int(self.order[self.courses % len(COURSE_PAIRS)])
        pair = COURSE_PAIRS[group]
        choices = self.pairs[pair]
        prefix = choices[int(self.rng.integers(len(choices)))]
        self.courses += 1
        return group, prefix
