"""Balanced starting-state and replay proportions."""
# group=-1 表示自由局；非负 group 按当前训练模式的有序职业对列表编号。
from itertools import product
from math import ceil

from blacksmith import master as engine

FREE_FRACTION = .4
COURSE_ROLES = tuple(range(1, len(engine.ROLES)))
COURSE_PAIRS = tuple(product(COURSE_ROLES, repeat=2))
# 每层至少能存放 64 个样本，且一个混合 batch 能覆盖全部职业层。
COURSE_MASS = (1 - FREE_FRACTION) / len(COURSE_PAIRS)
MIN_MIXED_BATCH = ceil(len(COURSE_PAIRS) / (1 - FREE_FRACTION))
MIN_MIXED_REPLAY = ceil(64 / COURSE_MASS)


def course_pairs(mode: str) -> tuple[tuple[int, int], ...]:
    if mode == "phantombook":
        role = engine.ROLES.index("phantombook")
        # 保留两个席位方向，排除同职业镜像，让专项面对其他职业的可借用技能。
        return tuple(pair for pair in COURSE_PAIRS if role in pair and pair != (role, role))
    return COURSE_PAIRS


def free_fraction(mode: str) -> float:
    return .5 if mode == "phantombook" else FREE_FRACTION


def masses(mode: str) -> dict[int, float]:
    # 这是目标损失质量，也用于分配 replay 容量；不等于自然终局的出现频率。
    if mode == "free":
        return {-1: 1.0}
    if mode == "mixed":
        return {-1: FREE_FRACTION, **dict.fromkeys(range(len(COURSE_PAIRS)), COURSE_MASS)}
    if mode == "phantombook":
        pairs = course_pairs(mode)
        return {-1: .5, **dict.fromkeys(range(len(pairs)), .5 / len(pairs))}
    raise ValueError("Unknown replay mode")
