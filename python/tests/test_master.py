"""Native schema, history, network and self-play contracts on CPU."""
import argparse
import json
from collections import Counter
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch
from blacksmith import master as engine

from master.models import ModelConfig, Policy, checkpoint, load, masked_log_probs
from master.train.curriculum import action_named, build
from master.train.replay import Replay, Starts, StratifiedReplay
from master.train.search import search, solve_matrix
from master.train.sampling import COURSE_PAIRS, MIN_MIXED_BATCH, MIN_MIXED_REPLAY
from master.train import train

ROOT = Path(__file__).resolve().parents[2]


class MasterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        torch.set_num_threads(1)
        torch.manual_seed(42)

    def model(self) -> Policy:
        return Policy(config=ModelConfig(hidden=16, depth=2, history_width=16,
                                         history_heads=2, history_layers=1))

    def test_schema_and_action_table(self) -> None:
        self.assertEqual(engine.ROLES, ["none", "warlock", "cannon", "driver", "lancer", "bloodsigil",
                                      "holybook", "phantombook", "crossbow"])
        self.assertEqual(engine.SCHEMA_VERSION, 8)
        self.assertEqual(engine.COMMON_ENCODING_LENGTH, 266)
        self.assertEqual(engine.PROFESSION_ENCODING_LENGTH, 382)
        self.assertEqual(engine.OBSERVATION_LENGTH, 1458)
        self.assertEqual(engine.SKILL_NUM, 266)
        self.assertEqual(engine.SKILL_NUM, 2 * engine.BASE_ACTION_COUNT)
        for name in engine.ACTION_NAMES:
            self.assertNotIn('prophet', name)
            self.assertNotIn('monk', name)
        self.assertEqual(engine.HISTORY_OFFSET, 2 * (engine.COMMON_ENCODING_LENGTH + engine.PROFESSION_ENCODING_LENGTH) + 2)
        self.assertEqual(engine.OBSERVATION_LENGTH, engine.HISTORY_OFFSET + engine.HISTORY_LENGTH * engine.HISTORY_TOKEN_LENGTH)
        self.assertEqual(len(set(engine.ACTIONS)), engine.SKILL_NUM)
        for index, chain in enumerate(engine.ACTIONS):
            self.assertEqual(engine.parse_action(engine.ACTION_NAMES[index]), index)
            args = [value for node in chain for value in node]
            self.assertEqual(engine.action_index(*args), index)
        with self.assertRaises(ValueError):
            engine.action_index(256)
        observations, masks = engine.Arena(2, 1).observe()
        self.assertEqual(observations.dtype, np.float32)
        self.assertEqual(masks.shape, (2, 2, engine.SKILL_NUM))
        self.assertEqual(engine.Arena(1, 1).metadata().dtype, np.int32)
        self.assertFalse(observations[..., engine.HISTORY_OFFSET:].any())
        for layers in (1, 2):
            model = Policy(ModelConfig(hidden=16, depth=2, history_width=16,
                                       history_heads=2, history_layers=layers))
            logits, values = model(torch.from_numpy(observations))
            self.assertEqual(tuple(logits.shape), masks.shape)
            self.assertEqual(tuple(values.shape), (2, 2))
            self.assertTrue(torch.isfinite(logits).all())

    def test_side_equipment_preserves_role_and_profession_exclusion(self) -> None:
        iron = action_named("iron")
        warlock = action_named("warlock")
        cauldron = action_named("cauldron")
        alchemy = action_named("alchemy")
        crossbow = action_named("crossbow")
        fire = action_named("fire")
        magic = action_named("magic")
        midastouch = action_named("midastouch")
        profession_flag = 2 * engine.COMMON_ENCODING_LENGTH + 3

        arena = engine.Arena(1, 1)
        arena.reset(0, [[iron, iron]] * 6)
        self.assertEqual(arena.observe()[1][0, 0, alchemy], 0)
        arena.step(np.array([[cauldron, iron]], np.int32))
        observations, masks = arena.observe()
        self.assertEqual(int(arena.metadata()[0, 1]), engine.ROLES.index("none"))
        self.assertEqual(observations[0, 0, profession_flag], 0)
        self.assertEqual(masks[0, 0, warlock], 1)
        self.assertEqual(masks[0, 0, cauldron], 0)
        arena.step(np.array([[warlock, iron]], np.int32))
        observations, masks = arena.observe()
        self.assertEqual(int(arena.metadata()[0, 1]), engine.ROLES.index("warlock"))
        self.assertEqual(observations[0, 0, profession_flag], 1)
        self.assertEqual(masks[0, 0, crossbow], 0)
        self.assertEqual(masks[0, 0, fire], 1)
        self.assertEqual(masks[0, 0, magic], 1)

        arena.reset(0, [[iron, iron]] * 7)
        for move in (warlock, cauldron, alchemy, iron):
            self.assertEqual(arena.observe()[1][0, 0, move], 1)
            arena.step(np.array([[move, iron]], np.int32))
            self.assertEqual(int(arena.metadata()[0, 1]), engine.ROLES.index("warlock"))
        _, masks = arena.observe()
        for move in (fire, magic, midastouch):
            self.assertEqual(masks[0, 0, move], 1)
        for move in (warlock, crossbow, cauldron, alchemy):
            self.assertEqual(masks[0, 0, move], 0)

    def test_active_professions_and_makebolts_parameters(self) -> None:
        iron = action_named("iron")
        for role, name in enumerate(engine.ROLES[1:], 1):
            arena = engine.Arena(1, 1)
            arena.reset(0, [[iron, iron]] * 7)
            arena.step(np.array([[action_named(name), iron]], np.int32))
            self.assertEqual(int(arena.metadata()[0, 1]), role)
        arena.reset(0, [[iron, iron]] * 7 + [[action_named("crossbow"), iron]])
        bolts = engine.parse_action("makebolts 5")
        self.assertEqual(arena.observe()[1][0, 0, bolts], 1)
        arena.step(np.array([[bolts, iron]], np.int32))
        self.assertEqual(arena.status()["players"][0]["bolt"], 15)
        self.assertEqual(arena.status()["players"][0]["iron"], 0)

    def test_association_mask_branch_history_and_network(self) -> None:
        iron = action_named("iron")
        arena = engine.Arena(1, 1)
        arena.reset(0, [[iron, iron]] * 5 + [
            [action_named("phantombook"), action_named("warlock")],
            [action_named("fantasia"), action_named("magic")],
            [action_named("fantasia"), action_named("magic")],
        ])
        borrowed = engine.parse_action("association 0 magicattack 2")
        impossible = engine.parse_action("association 0 magicattack 3")
        owned = engine.parse_action("association 0 iron 0")
        before, masks = arena.observe()
        self.assertEqual(masks[0, 0, borrowed], 1)
        self.assertEqual(masks[0, 0, impossible], 0)
        self.assertEqual(masks[0, 0, owned], 0)
        choices = np.array([[[borrowed], [iron]]], np.int32)
        branch, _, _ = engine.SearchTree(arena).expand(choices)
        np.testing.assert_array_equal(before, arena.observe()[0])
        arena.step(choices[..., 0], reset_finished=False)
        actual, _ = arena.observe()
        np.testing.assert_array_equal(actual, branch)
        status = arena.status()["players"]
        self.assertEqual(status[0]["dream"], 0)
        self.assertEqual(status[0]["magic"], 0)
        self.assertEqual(status[1]["magic"], 2)
        self.assertEqual(status[1]["hp"], 6)
        history = actual[0, 0, engine.HISTORY_OFFSET:].reshape(engine.HISTORY_LENGTH, -1)
        self.assertEqual(int(history[-1, 1]), borrowed)
        model = self.model()
        logits, values = model(torch.from_numpy(actual))
        (logits[0, 0, borrowed] + values.sum()).backward()
        self.assertGreater(float(model.history.actions.weight.grad[borrowed].abs().sum()), 0)
        self.assertGreater(float(model.history.base_actions.weight.grad[
            borrowed - engine.BASE_ACTION_COUNT].abs().sum()), 0)
        arena.step(np.array([[iron, iron]], np.int32), reset_finished=False)
        self.assertEqual(arena.status()["players"][1]["hp"], 2)

    def test_shared_association_head_search_and_policy_gradient(self) -> None:
        arena = engine.Arena(1, 1)
        iron = action_named('iron')
        arena.reset(0, [[iron, iron]] * 5 + [
            [action_named('phantombook'), action_named('warlock')],
            [action_named('fantasia'), action_named('magic')],
            [action_named('fantasia'), action_named('magic')]])
        borrowed = engine.parse_action('association 0 magicattack 2')
        base = borrowed - engine.BASE_ACTION_COUNT
        model = self.model().eval()
        states, masks = arena.observe()
        x = torch.from_numpy(states)
        logits, _ = model(x)
        torch.testing.assert_close(logits[..., :engine.BASE_ACTION_COUNT],
                                   logits[..., engine.BASE_ACTION_COUNT:])
        model.zero_grad(set_to_none=True)
        loss = -masked_log_probs(logits, torch.from_numpy(masks))[0, 0, borrowed]
        loss.backward()
        for layer in (model.actor, model.association_actor):
            self.assertGreater(float(layer.weight.grad[base].abs().sum()), 0)
        self.assertGreater(float(model.association_gate.weight.grad.abs().sum()), 0)
        probabilities = masked_log_probs(logits, torch.from_numpy(masks)).exp()
        self.assertEqual(float(probabilities[0, 0, base].detach()), 0)  # Unowned normal skill.
        self.assertGreater(float(probabilities[0, 0, borrowed].detach()), 0)
        with torch.no_grad():
            model.actor.weight.zero_()
            model.actor.bias.zero_()
            model.actor.bias[iron] = 10
            model.association_actor.bias[base] = 50
        actions, policies = search(model, arena, np.random.default_rng(42),
                                    torch.device('cpu'), 1, 1, 8)
        self.assertEqual(int(actions[0, 0]), borrowed)
        self.assertEqual(float(policies[0, 0, borrowed]), 1)
        np.testing.assert_array_equal(arena.observe()[0], states)

    def test_history_required_and_checkpoint_architecture_enforced(self) -> None:
        for layers in (0, -1):
            with self.subTest(layers=layers), self.assertRaises(ValueError):
                ModelConfig(history_layers=layers)
        model = self.model()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'model.pt'
            for architecture in (None, 'unsupported'):
                payload = checkpoint(model)
                if architecture is None:
                    del payload['architecture']
                else:
                    payload['architecture'] = architecture
                torch.save(payload, path)
                with self.subTest(architecture=architecture), self.assertRaisesRegex(ValueError, 'architecture'):
                    load(path)

    @unittest.skip("Prophet is temporarily disabled")
    def test_crystal_wall_hit_marks_expire_and_refund_once_per_round(self) -> None:
        iron = action_named("iron")
        magicattack = next(index for index, name in enumerate(engine.ACTION_NAMES)
                           if name.casefold() == "magicattack 1")
        arena = engine.Arena(1, 1)
        arena.reset(0, [[iron, iron]] * 6 + [
            [action_named("prophet"), action_named("warlock")],
            [action_named("crystal"), action_named("magic")],
            [action_named("crystal"), iron],
            [action_named("crystalball"), iron],
        ])
        profession_offset = 2 * engine.COMMON_ENCODING_LENGTH
        crystal_offset = profession_offset + engine.PROFESSION_ENCODING_LENGTH - 24 + 9
        mark_slice = slice(profession_offset + engine.MARK_OFFSET,
                           profession_offset + engine.PENDING_RESOURCE_OFFSET)
        # The second wall round receives a delayed magic hit and a new physical
        # hit. Each round refunds one Crystal, and its hit mark then expires.
        for move, attack, crystals in (
            (action_named("crystalwall"), magicattack, 1),
            (iron, action_named("stick"), 2),
            (iron, iron, 2),
        ):
            arena.step(np.array([[move, attack]], np.int32))
            observations, _ = arena.observe()
            self.assertAlmostEqual(float(observations[0, 0, crystal_offset]), crystals / 10)
            self.assertFalse(observations[0, 0, mark_slice].any())

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
        loss = policy_loss + torch.nn.functional.binary_cross_entropy(values, torch.tensor([1., 0., 1., 0.]))
        loss.backward()
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

    def test_residual_probability_head_and_checkpoint(self) -> None:
        arena = engine.Arena(2, 1)
        arena.step(np.zeros((2, 2), np.int32))
        obs, masks = arena.observe()
        x = torch.from_numpy(obs)
        model = Policy(config=ModelConfig(hidden=16, depth=4, history_width=16,
                       history_layers=1, history_heads=2))
        logits, probability = model(x)
        torch.testing.assert_close(probability, torch.full((2, 2), .5))
        _, raw = model(x, value_logits=True)
        torch.testing.assert_close(raw.sigmoid(), probability)
        loss = torch.nn.functional.binary_cross_entropy_with_logits(raw,
                          torch.tensor([[1., 0.], [.5, .5]])) + logits.square().mean()
        loss.backward()
        self.assertTrue(all(torch.isfinite(p.grad).all() for p in model.parameters() if p.grad is not None))
        self.assertGreater(float(model.body[2].branch[1].weight.grad.abs().sum()), 0)
        torch.testing.assert_close(model.signed_values(torch.tensor([0., .5, 1.])),
                                   torch.tensor([-1., 0., 1.]))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "residual.pt"
            torch.save(checkpoint(model), path)
            restored = load(path)
            for a, b in zip(restored(x), model.eval()(x)):
                torch.testing.assert_close(a, b)
        actions, policies = search(model.eval(), arena, np.random.default_rng(42),
                                    torch.device("cpu"), 2, 1, 8)
        self.assertTrue((masks[np.arange(2)[:, None], np.arange(2), actions] == 1).all())
        np.testing.assert_allclose(policies.sum(-1), 1, atol=1e-6)

    def test_training_gradient_budget_and_safe_optimizer_checkpoint(self) -> None:
        with tempfile.TemporaryDirectory(dir=ROOT / 'out') as folder:
            parser = argparse.ArgumentParser()
            train.configure(parser)
            args = parser.parse_args(['--mode', 'free', '--output', str(Path(folder) / 'run'),
                '--device', 'cpu',
                '--hidden', '16', '--depth', '4', '--history-width', '16',
                '--history-layers', '1', '--history-heads', '2', '--envs', '2',
                '--threads', '1', '--width', '2', '--leaf-width', '1', '--regret-iterations', '8',
                '--horizon', '32', '--updates', '100', '--warmup', '1', '--batches', '4',
                '--batch-size', '16', '--max-gradient-steps', '1', '--max-seconds', '30'])
            train.run(args)
            self.assertFalse(list(Path(args.output).glob('*.csv')))
            self.assertFalse(list(Path(args.output).glob('*.jsonl')))
            completion = json.loads((Path(args.output) / 'completion.json').read_text())
            self.assertEqual(completion['stop_reason'], 'gradient_budget')
            self.assertEqual(completion['gradient_steps'], 1)
            payload = torch.load(Path(args.output) / completion['checkpoint'], weights_only=True)
            self.assertIsInstance(payload['optimizer']['param_groups'][0]['lr'], float)
            optimizer = torch.optim.AdamW(load(Path(args.output) / completion['checkpoint']).parameters())
            optimizer.load_state_dict(payload['optimizer'])
            self.assertTrue(optimizer.state)
            # A free-only continuation retains Adam moments and cumulative steps,
            # while all newly generated episodes and replay samples are free.
            previous_checkpoint = Path(args.output) / completion['checkpoint']
            args.resume = str(previous_checkpoint)
            args.output = str(Path(folder) / 'free-continuation')
            args.max_gradient_steps = 2
            args.lr_time_offset = 10
            args.record_games = True
            train.run(args)
            resumed = json.loads((Path(args.output) / 'completion.json').read_text())
            self.assertEqual(resumed['gradient_steps'], 2)
            self.assertGreater(resumed['games'], completion['games'])
            self.assertEqual(set(resumed['pool_sizes']), {'-1'})
            starts = [json.loads(row) for row in (Path(args.output) / 'starts.jsonl').read_text().splitlines()]
            self.assertTrue(all(row['group'] == -1 and row['prefix'] == [] for row in starts))
            initial = torch.load(Path(args.output) / f"master-{payload['update']:04d}.pt", weights_only=True)
            final = torch.load(Path(args.output) / resumed['checkpoint'], weights_only=True)
            for key, state in payload['optimizer']['state'].items():
                torch.testing.assert_close(initial['optimizer']['state'][key]['exp_avg'], state['exp_avg'])
                torch.testing.assert_close(final['optimizer']['state'][key]['step'], state['step'] + 1)

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
        self.assertEqual(len(entries), len(COURSE_PAIRS))
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
            groups = [starts.next()[0] for _ in range(10 * len(COURSE_PAIRS))]
            self.assertEqual(groups.count(-1), 4 * len(COURSE_PAIRS))
            counts = Counter(group for group in groups if group != -1)
            self.assertEqual(counts, dict.fromkeys(range(len(COURSE_PAIRS)), 6))
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
        pool = StratifiedReplay("mixed", MIN_MIXED_REPLAY)
        for group in pool.pools:
            pool.add(group, [(observations[0], masks[0], policies[0])] * 40, 3)
            pool.pools[group].states[:, 0] = group
        self.assertTrue(pool.ready(64))
        for size in (MIN_MIXED_BATCH, 2 * MIN_MIXED_BATCH):
            x, _, _, _, weights = pool.sample(np.random.default_rng(1), size)
            self.assertAlmostEqual(float(weights.sum()), 1, places=6)
            self.assertAlmostEqual(float(weights[x[:, 0] == -1].sum()), .4, places=6)
            for group in range(len(COURSE_PAIRS)):
                self.assertAlmostEqual(float(weights[x[:, 0] == group].sum()), .6 / len(COURSE_PAIRS), places=6)

    def test_curriculum_rechecks_roles_after_dreamwake(self) -> None:
        from master.train import curriculum
        iron, fantasia, rewind = (action_named(name) for name in ('iron', 'fantasia', 'dreamwake'))
        pair = (engine.ROLES.index('phantombook'), engine.ROLES.index('warlock'))

        class ForceRewind:
            batches = 0

            def integers(self, low, high, size):
                if high == 5:
                    return np.full(size, 4)
                self.batches += 1
                return np.full(size, 3 if self.batches == 1 else 0)

            def choice(self, legal):
                for action in (rewind, fantasia, iron):
                    if action in legal:
                        return action
                raise AssertionError('No scripted legal move')

        rng = ForceRewind()
        with patch.object(curriculum, 'COURSE_PAIRS', (pair,)), \
             patch.object(curriculum.np.random, 'default_rng', return_value=rng):
            entries = curriculum.build(1)
        arena = engine.Arena(1, 1)
        for entry in entries:
            arena.reset(0, entry.prefix)
            self.assertEqual(tuple(arena.metadata()[0, 1:]), entry.roles)
        self.assertEqual(rng.batches, 2)


if __name__ == "__main__":
    unittest.main()
