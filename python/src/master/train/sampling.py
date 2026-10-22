"""Balanced starting-state and replay proportions."""
from itertools import product
from math import ceil

from blacksmith import master as engine

FREE_FRACTION = .4
COURSE_ROLES = tuple(range(1, len(engine.ROLES)))
COURSE_PAIRS = tuple(product(COURSE_ROLES, repeat=2))
COURSE_MASS = (1 - FREE_FRACTION) / len(COURSE_PAIRS)
MIN_MIXED_BATCH = ceil(len(COURSE_PAIRS) / (1 - FREE_FRACTION))
MIN_MIXED_REPLAY = ceil(64 / COURSE_MASS)


def masses(mode: str) -> dict[int, float]:
    if mode == "free":
        return {-1: 1.0}
    if mode == "mixed":
        return {-1: FREE_FRACTION, **dict.fromkeys(range(len(COURSE_PAIRS)), COURSE_MASS)}
    raise ValueError("Unknown replay mode")
