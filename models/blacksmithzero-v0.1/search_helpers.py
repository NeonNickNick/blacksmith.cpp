"""Depth-two simultaneous matrix search, using only a frozen self-play network."""
import ctypes
import numpy as np
import torch
from training_core import solve_matrix


def expand(arena, choices, retain):
    parents, _, width = choices.shape
    key = (parents, width)
    if not hasattr(arena, "tree_buffers"):
        arena.tree_buffers = {}
        floats = np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
        integers = np.ctypeslib.ndpointer(dtype=np.int32, flags="C_CONTIGUOUS")
        arena.lib.zt_expand.argtypes = [ctypes.c_int, integers, ctypes.c_int, floats, floats, floats]
    if key not in arena.tree_buffers:
        cells = parents * width * width
        arena.tree_buffers[key] = (np.zeros((cells, 2, 718), np.float32),
                                  np.zeros((cells, 2, 156), np.float32),
                                  np.zeros((parents, width, width), np.float32))
    observations, masks, outcomes = arena.tree_buffers[key]
    assert arena.lib.zt_expand(width, choices, int(retain), observations, masks, outcomes) == 0
    return observations, masks, outcomes


def matrix(payoffs, choices, iterations):
    valid = (choices >= 0).astype(np.float32)
    # Entirely padded/terminal parents contribute zero and need a dummy distribution.
    for seat in range(2):
        empty = valid[:, seat].sum(-1) == 0
        valid[empty, seat, 0] = 1
    p, q = solve_matrix(payoffs, valid[:, 0], valid[:, 1], iterations)
    value = np.einsum("bi,bij,bj->b", p, payoffs, q)
    return p, q, value


def terminals(payoffs, outcomes):
    payoffs[outcomes == 1] = 1
    payoffs[outcomes == 2] = -1
    payoffs[(outcomes == 3) | (outcomes == 4) | (outcomes == -1)] = 0


