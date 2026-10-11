"""Native schema, history, network and self-play contracts on CPU."""
import json
from collections import Counter
from pathlib import Path
import tempfile
import unittest

import numpy as np
import torch
from blacksmith import master as engine

from master.models import ModelConfig, Policy, checkpoint, load, masked_log_probs
from master.train.curriculum import action_named, build
from master.train.replay import Replay, Starts, StratifiedReplay
from master.train.search import search, solve_matrix
from master.train.sampling import COURSE_PAIRS, MIN_MIXED_BATCH, MIN_MIXED_REPLAY

ROOT = Path(__file__).resolve().parents[2]


class MasterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        torch.set_num_threads(1)
        torch.manual_seed(42)

    def model(self, history_layers: int = 1) -> Policy:
        return Policy(config=ModelConfig(hidden=16, depth=2, history_width=16,
                                         history_heads=2, history_layers=history_layers))

    def test_schema_and_action_table(self) -> None:
        self.assertEqual(engine.ROLES, ["none", "warlock", "cannon", "driver", "lancer", "bloodsigil"])
        self.assertEqual(engine.STATE_FEATURES, 2 * (engine.COMMON_ENCODING_LENGTH + engine.PROFESSION_ENCODING_LENGTH) + 2)
        self.assertEqual(engine.OBSERVATION_LENGTH, engine.STATE_FEATURES + engine.HISTORY_LENGTH * engine.HISTORY_TOKEN_LENGTH)
        self.assertEqual(len(set(engine.ACTIONS)), engine.SKILL_NUM)
        for index, (move, parameter) in enumerate(engine.ACTIONS):
            self.assertEqual(engine.action_index(move, parameter), index)
        with self.assertRaises(ValueError):
            engine.action_index(256)
        observations, masks = engine.Arena(2, 1).observe()
        self.assertEqual(observations.dtype, np.float32)
        self.assertEqual(masks.shape, (2, 2, engine.SKILL_NUM))
        self.assertEqual(engine.Arena(1, 1).metadata().dtype, np.int32)
        self.assertFalse(observations[..., engine.HISTORY_OFFSET:].any())
        for layers in (0, 1):
            logits, values = self.model(layers)(torch.from_numpy(observations))
            self.assertEqual(tuple(logits.shape), masks.shape)
            self.assertEqual(tuple(values.shape), (2, 2))
            self.assertTrue(torch.isfinite(logits).all())

    def test_history_perspectives_branch_copy_truncation_and_reset(self) -> None:
        arena = engine.Arena(1, 1)
        iron, stick = action_named("iron"), action_named("stick")
        arena.step(np.array([[iron, iron]], np.int32))
        choices = np.array([[[stick], [iron]]], np.int32)
        branch, _, _ = engine.SearchTree(arena).expand(choices)
        before, _ = arena.observe()
        self.assertEqual(before[0, 0, -engine.HISTORY_TOKEN_LENGTH], 1)
        arena.step(choices[..., 0])
        actual, _ = arena.observe()
        np.testing.assert_array_equal(branch, actual)
        history = actual[0, :, engine.HISTORY_OFFSET:].reshape(2, engine.HISTORY_LENGTH, engine.HISTORY_TOKEN_LENGTH)
        self.assertEqual(int(history[0, -1, 1]), stick)
        self.assertEqual(int(history[1, -1, 2]), stick)
        np.testing.assert_array_equal(history[0, -1, 4:12], history[1, -1, 12:20])
        np.testing.assert_array_equal(before[..., engine.HISTORY_OFFSET:].reshape(
            1, 2, engine.HISTORY_LENGTH, engine.HISTORY_TOKEN_LENGTH)[..., 0].sum(-1), 1)
        arena.reset(0)
        for _ in range(engine.HISTORY_LENGTH + 3):
            arena.step(np.array([[iron, iron]], np.int32))
        past = arena.observe()[0][0, 0, engine.HISTORY_OFFSET:].reshape(engine.HISTORY_LENGTH, -1)
        np.testing.assert_allclose(past[:, 3], np.arange(4, 12) / engine.ROUND_LIMIT)
        arena.reset(0, [[iron, iron]] * (engine.ROUND_LIMIT - 1))
        self.assertEqual(int(arena.step(np.array([[iron, iron]], np.int32))[0, 0]), 4)
        self.assertFalse(arena.observe()[0][..., engine.HISTORY_OFFSET:].any())
        self.assertEqual(int(arena.metadata()[0, 0]), 1)

    def test_padding_gradients_and_checkpoint(self) -> None:
        arena = engine.Arena(2, 1)
        arena.step(np.zeros((2, 2), np.int32))
        observations, masks = arena.observe()
        x = torch.from_numpy(observations.reshape(-1, engine.OBSERVATION_LENGTH))
        model = self.model().eval()
        poisoned = x.clone()
        past = poisoned[:, engine.HISTORY_OFFSET:].reshape(-1, engine.HISTORY_LENGTH, engine.HISTORY_TOKEN_LENGTH)
        past[:, :-1, 1:] = float("nan")
        with torch.no_grad():
            expected = model(x)
            actual = model(poisoned)
            for a, b in zip(actual, expected):
                torch.testing.assert_close(a, b)
            no_history = x.clone()
            no_history[:, engine.HISTORY_OFFSET:] = 0
            self.assertFalse(torch.allclose(model(x)[0], model(no_history)[0], atol=1e-8, rtol=0))
        model.train()
        optimizer = torch.optim.AdamW(model.parameters(), lr=.003)
        logits, values = model(x)
        policy_loss = -masked_log_probs(logits, torch.from_numpy(masks.reshape(-1, engine.SKILL_NUM)))[:, 0].mean()
        loss = policy_loss + (values - torch.tensor([1., -1., 1., -1.])).square().mean()
        loss.backward()
        assert model.history is not None
        self.assertGreater(float(model.history.actions.weight.grad.abs().sum()), 0)
        self.assertTrue(all(torch.isfinite(p.grad).all() for p in model.parameters() if p.grad is not None))
        optimizer.step()
        model.eval()
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder) / "model.pt"
            torch.save(checkpoint(model), destination)
            restored = load(destination)
            for a, b in zip(restored(x), model(x)):
                torch.testing.assert_close(a, b)
            payload = checkpoint(model)
            payload["schema_version"] += 1
            torch.save(payload, destination)
            with self.assertRaises(ValueError):
                load(destination)

    def test_search_legal_and_snapshot_unchanged(self) -> None:
        arena = engine.Arena(3, 2)
        model = self.model().eval()
        before, masks = arena.observe()
        actions, policies = search(model, arena, np.random.default_rng(42), torch.device("cpu"),
                                   width=3, leaf_width=2, iterations=16, exploration=.15)
        np.testing.assert_array_equal(before, arena.observe()[0])
        self.assertTrue((masks[np.arange(3)[:, None], np.arange(2), actions] == 1).all())
        np.testing.assert_allclose(policies.sum(-1), 1, atol=1e-6)
        self.assertTrue((policies[masks == 0] == 0).all())
        arena.step(actions)
        self.assertTrue((arena.metadata()[:, 0] == 2).all())

    @unittest.skipUnless(torch.cuda.is_available(), "CUDA is not available on this host")
    def test_cuda_search_gradient_and_cpu_checkpoint(self) -> None:
        arena = engine.Arena(2, 1)
        arena.step(np.zeros((2, 2), np.int32))
        observations, masks = arena.observe()
        model = self.model().to("cuda").eval()
        actions, _ = search(model, arena, np.random.default_rng(42), torch.device("cuda"),
                            width=2, leaf_width=2, iterations=8)
        self.assertTrue((masks[np.arange(2)[:, None], np.arange(2), actions] == 1).all())
        model.train()
        x = torch.as_tensor(observations, device="cuda")
        optimizer = torch.optim.AdamW(model.parameters(), lr=.003)
        logits, values = model(x)
        loss = logits.square().mean() + (values - .5).square().mean()
        loss.backward()
        self.assertTrue(all(torch.isfinite(p.grad).all() for p in model.parameters() if p.grad is not None))
        optimizer.step()
        model.eval()
        with tempfile.TemporaryDirectory() as folder:
            destination = Path(folder) / "gpu.pt"
            torch.save(checkpoint(model), destination)
            restored = load(destination)
            with torch.no_grad():
                for cpu, gpu in zip(restored(torch.from_numpy(observations)), model(x)):
                    torch.testing.assert_close(cpu, gpu.cpu(), atol=1e-5, rtol=1e-4)

    def test_validation_before_mutation_and_parallel_equivalence(self) -> None:
        serial, parallel = engine.Arena(7, 1), engine.Arena(7, 3)
        rng = np.random.default_rng(1)
        before, _ = serial.observe()
        invalid = np.zeros((7, 2), np.int32)
        invalid[-1, 0] = engine.SKILL_NUM
        with self.assertRaises(ValueError):
            serial.step(invalid)
        np.testing.assert_array_equal(serial.observe()[0], before)
        with self.assertRaises(ValueError):
            serial.reset(0, [[engine.SKILL_NUM, 0]])
        other = engine.Arena(1, 1)
        for _ in range(60):
            observations, masks = serial.observe()
            p_observations, p_masks = parallel.observe()
            np.testing.assert_array_equal(observations, p_observations)
            np.testing.assert_array_equal(masks, p_masks)
            choices = np.array([[rng.choice(np.flatnonzero(row)) for row in game] for game in masks], np.int32)
            np.testing.assert_array_equal(serial.step(choices), parallel.step(choices))
        self.assertEqual(int(other.metadata()[0, 0]), 1)

    def test_regret_matching_equilibrium_and_masks(self) -> None:
        payoffs = np.array([[[1, -1, 100], [-1, 1, 100], [-100, -100, -100]]], np.float32)
        valid = np.array([[1, 1, 0]], np.float32)
        first, second = solve_matrix(payoffs, valid, valid)
        np.testing.assert_allclose(first, [[.5, .5, 0]])
        np.testing.assert_allclose(second, [[.5, .5, 0]])
        dominated = np.array([[[2, 2], [0, 0]]], np.float32)
        first, _ = solve_matrix(dominated, np.ones((1, 2), np.float32), np.ones((1, 2), np.float32), 128)
        self.assertGreater(first[0, 0], .99)

    def test_curriculum_roles_and_replay_terminal_signs(self) -> None:
        entries = build(1, 42)
        self.assertEqual(len(entries), 25)
        self.assertEqual({entry.roles for entry in entries}, set(COURSE_PAIRS))
        arena = engine.Arena(1, 1)
        for entry in entries:
            arena.reset(0, entry.prefix)
            np.testing.assert_array_equal(arena.metadata()[0, 1:], entry.roles)
        with tempfile.TemporaryDirectory() as folder:
            bank = Path(folder) / "bank.json"
            bank.write_text(json.dumps({"schema_version": engine.SCHEMA_VERSION, "roles": engine.ROLES,
                "entries": [{"roles": entry.roles, "prefix": entry.prefix} for entry in entries]}), encoding="utf-8")
            starts = Starts("mixed", str(bank), 42)
            groups = [starts.next()[0] for _ in range(250)]
            self.assertEqual(groups.count(-1), 100)
            counts = Counter(group for group in groups if group != -1)
            self.assertEqual(counts, dict.fromkeys(range(25), 6))
            for start in range(0, len(groups), 10):
                self.assertEqual(groups[start:start + 10].count(-1), 4)
        observations, masks = arena.observe()
        policies = masks / masks.sum(-1, keepdims=True)
        replay = Replay(3)
        replay.add_game([(observations[0], masks[0], policies[0])] * 2, 1)
        np.testing.assert_allclose(replay.values, [-1, -.997, 1])
        with self.assertRaises(ValueError):
            StratifiedReplay("mixed", MIN_MIXED_REPLAY - 1)
        self.assertTrue(all(part.capacity >= 64 for part in
                            StratifiedReplay("mixed", MIN_MIXED_REPLAY).pools.values()))
        pool = StratifiedReplay("mixed", 4096)
        for group in pool.pools:
            pool.add(group, [(observations[0], masks[0], policies[0])] * 40, 3)
            pool.pools[group].states[:, 0] = group
        self.assertTrue(pool.ready(64))
        for size in (MIN_MIXED_BATCH, 64):
            x, _, _, _, weights = pool.sample(np.random.default_rng(1), size)
            self.assertAlmostEqual(float(weights.sum()), 1, places=6)
            self.assertAlmostEqual(float(weights[x[:, 0] == -1].sum()), .4, places=6)
            for group in range(25):
                self.assertAlmostEqual(float(weights[x[:, 0] == group].sum()), .6 / 25, places=6)


if __name__ == "__main__":
    unittest.main()
