"""Terminal-outcome replay with explicit free/curriculum sampling masses."""
# 未结束对局不提供价值标签；只把终局已知的整段轨迹写入对应分层。
import json
from pathlib import Path

import numpy as np

from blacksmith import master as engine
from master.models.data import Floats, History, Prefix
from .search import DISCOUNT
from .sampling import COURSE_PAIRS, MIN_MIXED_BATCH, MIN_MIXED_REPLAY, masses, course_pairs, free_fraction


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
                # 第二席反转胜负符号；越早的局面折扣越多，之后映射为向 0.5 收缩的目标。
                self.values[index] = value * (1 if seat == 0 else -1) * DISCOUNT ** (len(history) - turn - 1)
                self.cursor = (self.cursor + 1) % self.capacity
                # 固定容量环形缓冲：覆盖最旧样本，不随训练时长无限占用内存。
                self.size = min(self.capacity, self.size + 1)


class StratifiedReplay:
    def __init__(self, mode: str, capacity: int) -> None:
        self.mode = mode
        self.masses = masses(mode)
        if mode == "mixed" and capacity < MIN_MIXED_REPLAY:
            raise ValueError(f"Mixed replay requires capacity >= {MIN_MIXED_REPLAY}")
        if mode != "free" and any(int(capacity * mass) < 64 for mass in self.masses.values()):
            raise ValueError("Each replay stratum requires capacity >= 64")
        self.pools = {key: Replay(int(capacity * mass)) for key, mass in self.masses.items()}

    def add(self, group: int, history: History, outcome: int) -> None:
        self.pools[group].add_game(history, outcome)

    def ready(self, size: int) -> bool:
        # 每个目标分层都要有数据，不能用已就绪的少数职业代替整个采样分布。
        return all(pool.size >= 64 for pool in self.pools.values()) and sum(
            pool.size for pool in self.pools.values()) >= size

    def sample(self, rng: np.random.Generator, size: int) -> tuple[Floats, Floats, Floats, Floats, Floats]:
        if self.mode == "mixed" and size < MIN_MIXED_BATCH:
            raise ValueError(f"Mixed batches require at least {MIN_MIXED_BATCH} samples")
        pairs = course_pairs(self.mode)
        free = round(size * free_fraction(self.mode))
        if self.mode != "free" and size - free < len(pairs):
            raise ValueError("Batch must include every curriculum stratum")
        counts = {-1: size} if self.mode == "free" else {
            -1: free, **{i: (size - free) // len(pairs) +
                        (i < (size - free) % len(pairs)) for i in range(len(pairs))}}
        arrays: list[list[Floats]] = [[], [], [], [], []]
        for group, count in counts.items():
            pool = self.pools[group]
            indices = rng.integers(pool.size, size=count)
            for output, source in zip(arrays[:4], (pool.states, pool.masks, pool.policies, pool.values)):
                output.append(source[indices])
            # 整数配额会产生舍入；每层总损失权重仍严格等于它的目标质量。
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
        self.course_pairs = course_pairs(mode)
        self.order = np.arange(len(self.course_pairs))
        self.pairs: dict[tuple[int, int], list[Prefix]] = {}
        if mode != "free":
            if bank is None:
                raise ValueError("Mixed self-play requires a curriculum bank")
            payload = json.loads(Path(bank).read_text(encoding="utf-8"))
            if payload["schema_version"] != engine.SCHEMA_VERSION:
                # 前缀保存动作编号，动作表或职业顺序不兼容时不能复用旧课程。
                raise ValueError("Curriculum action schema mismatch")
            if payload.get("roles") != engine.ROLES:
                raise ValueError("Curriculum profession schema mismatch")
            for entry in payload["entries"]:
                first, second = entry["roles"]
                if (first, second) not in COURSE_PAIRS:
                    raise ValueError("Curriculum entries must use active professions")
                self.pairs.setdefault((first, second), []).append(entry["prefix"])
            if any(pair not in self.pairs for pair in self.course_pairs):
                raise ValueError(f"Curriculum bank must cover all {len(self.course_pairs)} ordered profession pairs")

    def next(self) -> tuple[int, Prefix]:
        # 以十个新开局为一个配比周期；局长不同，完成局数不一定也正好符合该比例。
        course = self.mode != "free" and self.started % 10 >= round(10 * free_fraction(self.mode))
        self.started += 1
        if not course:
            return -1, []
        if self.courses % len(self.course_pairs) == 0:
            # 每轮都覆盖所有职业对，轮内顺序随机，避免某个席位/职业长期缺样本。
            self.rng.shuffle(self.order)
        group = int(self.order[self.courses % len(self.course_pairs)])
        pair = self.course_pairs[group]
        choices = self.pairs[pair]
        prefix = choices[int(self.rng.integers(len(choices)))]
        self.courses += 1
        return group, prefix
