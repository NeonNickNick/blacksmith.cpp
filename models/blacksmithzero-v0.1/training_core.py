"""Matrix regret matching and terminal-outcome replay."""
import numpy as np

def solve_matrix(payoffs, row_valid, column_valid, iterations=64):
    """Regret matching+; average independent policies, no current-action leakage."""
    row_regret = np.zeros(payoffs.shape[:2], np.float32)
    column_regret = np.zeros((payoffs.shape[0], payoffs.shape[2]), np.float32)
    row_sum, column_sum = np.zeros_like(row_regret), np.zeros_like(column_regret)
    row_uniform = row_valid / row_valid.sum(-1, keepdims=True)
    column_uniform = column_valid / column_valid.sum(-1, keepdims=True)
    for iteration in range(iterations):
        row_total, column_total = row_regret.sum(-1, keepdims=True), column_regret.sum(-1, keepdims=True)
        row = np.where(row_total > 1e-8, row_regret / np.maximum(row_total, 1e-8), row_uniform)
        column = np.where(column_total > 1e-8, column_regret / np.maximum(column_total, 1e-8), column_uniform)
        row_value = np.einsum("bij,bj->bi", payoffs, column)
        column_value = -np.einsum("bi,bij->bj", row, payoffs)
        value = (row * row_value).sum(-1, keepdims=True)
        row_regret = np.maximum(row_regret + row_value - value, 0) * row_valid
        column_regret = np.maximum(column_regret + column_value + value, 0) * column_valid
        weight = iteration + 1
        row_sum += weight * row
        column_sum += weight * column
    row = row_sum / row_sum.sum(-1, keepdims=True)
    column = column_sum / column_sum.sum(-1, keepdims=True)
    return row, column


class Replay:
    def __init__(self, capacity):
        self.states = np.empty((capacity, 718), np.float32)
        self.masks = np.empty((capacity, 156), np.float32)
        self.policies = np.empty((capacity, 156), np.float32)
        self.values = np.empty(capacity, np.float32)
        self.capacity, self.cursor, self.size = capacity, 0, 0

    def add_game(self, history, result):
        value = 1 if result == 1 else -1 if result == 2 else 0
        length = len(history)
        for turn, (observations, masks, policies) in enumerate(history):
            for seat in range(2):
                index = self.cursor
                self.states[index] = observations[seat]
                self.masks[index] = masks[seat]
                self.policies[index] = policies[seat]
                self.values[index] = value * (1 if seat == 0 else -1) * 0.997 ** (length - turn - 1)
                self.cursor = (self.cursor + 1) % self.capacity
                self.size = min(self.capacity, self.size + 1)


