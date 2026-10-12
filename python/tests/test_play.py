"""Per-game model selection and bounded simultaneous search contracts."""
import argparse
import contextlib
import io
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import torch
from blacksmith import master as engine
from master import play
from master.agent import Agent as MasterAgent
from master.models import ModelConfig, Policy
from master.search import SearchInfo, search
from master.train.search import search as training_search


class ToyModel(torch.nn.Module):
    def forward(self, states):
        x = states[..., 0]
        values = torch.where(x.abs() == 1, .8 * x.sign(), -.8 * x.sign())
        return torch.zeros((*x.shape, engine.SKILL_NUM)), values

    def signed_values(self, values):
        return values


class ToyArena:
    count = 1

    def observe(self):
        states = np.zeros((1, 2, engine.OBSERVATION_LENGTH), np.float32)
        masks = np.zeros((1, 2, engine.SKILL_NUM), np.float32)
        masks[..., :2] = 1
        return states, masks


class ToyTree:
    def __init__(self, arena=None, parents=None):
        self.parents = [0] if parents is None else parents

    def subset(self, indices):
        return ToyTree(parents=[self.parents[i] for i in indices])

    def expand(self, choices, retain=True):
        width = choices.shape[-1]
        states = np.zeros((len(self.parents) * width**2, 2, engine.OBSERVATION_LENGTH), np.float32)
        masks = np.zeros((len(states), 2, engine.SKILL_NUM), np.float32)
        outcomes = np.zeros((len(self.parents), width, width), np.float32)
        children = []
        for row, parent in enumerate(self.parents):
            for a in range(width):
                for b in range(width):
                    index = row * width**2 + a * width + b
                    child = int(choices[row, 0, a]) + 1 if parent == 0 else parent
                    children.append(child)
                    if parent:
                        outcomes[row, a, b] = 2 if parent == 1 else 1
                    else:
                        states[index, :, 0] = (child, -child)
                        masks[index, :, 0] = 1
        if retain:
            self.parents = children
        return states, masks, outcomes


class PlayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)

    def test_deeper_search_overrides_misleading_value_and_budget_keeps_completed_depth(self):
        with patch.object(engine, 'SearchTree', ToyTree):
            args = (ToyModel(), ToyArena(), np.random.default_rng(42), torch.device('cpu'))
            _, shallow, shallow_info = search(*args, width=2, leaf_width=1, iterations=64, depth=1)
            self.assertGreater(shallow[0, 0, 0], .98)
            args = (ToyModel(), ToyArena(), np.random.default_rng(42), torch.device('cpu'))
            _, deep, info = search(*args, width=2, leaf_width=1, iterations=64, depth=3, max_nodes=12, batch_cells=1)
            self.assertGreater(deep[0, 0, 1], .98)
            self.assertEqual(info.completed_depth, 2)
            self.assertTrue(info.budget_exhausted)
            self.assertLessEqual(info.expanded_nodes, 12)
            self.assertEqual(shallow_info.completed_depth, 1)

    def test_native_subset_and_two_round_search_equivalence(self):
        arena = engine.Arena(1, 1)
        before, masks = arena.observe()
        model = Policy(config=ModelConfig(hidden=16, depth=2, history_width=16,
                                         history_layers=0, history_heads=2)).eval()
        with torch.no_grad():
            model.critic.weight.normal_(0, .2)
        expected_actions, expected = training_search(model, arena, np.random.default_rng(12),
                                                     torch.device('cpu'), 2, 1, 16)
        actions, actual, info = search(model, arena, np.random.default_rng(12), torch.device('cpu'),
                                      width=2, leaf_width=1, iterations=16, depth=2)
        np.testing.assert_allclose(actual, expected, atol=1e-6)
        np.testing.assert_array_equal(actions, expected_actions)
        np.testing.assert_array_equal(arena.observe()[0], before)
        self.assertEqual(info.completed_depth, 2)
        self.assertTrue((masks[np.arange(1)[:, None], np.arange(2), actions] == 1).all())
        tree = engine.SearchTree(arena)
        with self.assertRaises(IndexError):
            tree.subset([1])

    def test_native_terminal_depth_and_legality(self):
        arena = engine.Arena(1, 1)
        iron = engine.ACTION_NAMES.index('iron 0')
        arena.reset(0, [[iron, iron]] * (engine.ROUND_LIMIT - 1))
        model = Policy(config=ModelConfig(hidden=16, history_layers=0, history_heads=2))
        before, masks = arena.observe()
        actions, policies, info = search(model, arena, np.random.default_rng(42), torch.device('cpu'),
                                        width=3, leaf_width=2, depth=4, max_nodes=100)
        self.assertEqual(info.completed_depth, 4)
        np.testing.assert_array_equal(arena.observe()[0], before)
        self.assertTrue((masks[np.arange(1)[:, None], np.arange(2), actions] == 1).all())
        np.testing.assert_allclose(policies.sum(-1), 1)

    def test_model_pool_reproducibility_and_all_five_models(self):
        with tempfile.TemporaryDirectory() as folder:
            for i in range(5):
                (Path(folder) / f'master-{i+1:02d}.pt').touch()
            a = play.CheckpointPool(directory=folder, seed=42)
            b = play.CheckpointPool(directory=folder, seed=42)
            first = [a.next() for _ in range(100)]
            self.assertEqual(first, [b.next() for _ in range(100)])
            self.assertEqual(len({x[0] for x in first}), 5)
            fixed = play.CheckpointPool(checkpoint=first[0][0], seed=42)
            self.assertTrue(all(fixed.next()[0] == first[0][0] for _ in range(5)))

    def test_skill_input_mapping_covers_native_table_and_rejects_bad_parameters(self):
        for index, name in enumerate(engine.ACTION_NAMES):
            self.assertEqual(play.parse_action(name), index)
        self.assertEqual(play.parse_action('  IRON  '), play.parse_action('iron 0'))
        self.assertEqual(play.parse_action('shield 5'), engine.action_index(5, 5))
        for text in ('', '0', 'unknown 0', 'shield -1', 'shield 0.5',
                     'shield 6', 'shield 1 extra', 'iron 1', 'magicattack 0'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                play.parse_action(text)

    def test_play_starting_iron_is_real_and_seat_independent(self):
        arena = engine.Arena(1, 1)
        iron = play.parse_action('iron')
        stick = play.parse_action('stick')
        for seat in (0, 1):
            for amount in (0.0, 0.5, 1.0):
                arena.reset_play(0, seat, amount)
                state, masks = arena.observe()
                status = arena.status()
                self.assertEqual(status['round'], 1)
                self.assertEqual(status['players'][seat]['iron'], amount)
                self.assertEqual(status['players'][1-seat]['iron'], 0)
                self.assertAlmostEqual(state[0, seat, 3] * 10, amount)
                self.assertEqual(np.count_nonzero(state[..., engine.HISTORY_OFFSET:]), 0)
                self.assertEqual(bool(masks[0, seat, stick]), amount > 0)
                self.assertFalse(masks[0, 1-seat, stick])
                # Search branches inherit the handicap without changing the arena.
                moves = np.array([[[iron], [iron]]], np.int32)
                children, _, _ = engine.SearchTree(arena).expand(moves)
                self.assertAlmostEqual(children[0, seat, 3] * 10, amount + 1, places=5)
                self.assertEqual(arena.status()['players'][seat]['iron'], amount)
        for seat, amount in ((2, .5), (0, -.5), (1, float('nan'))):
            with self.assertRaises(ValueError):
                arena.reset_play(0, seat, amount)

    def test_prediction_score_normalizes_both_heads_and_uses_correct_seat(self):
        arena = engine.Arena(1, 1)
        agent = MasterAgent.__new__(MasterAgent)
        agent.device = torch.device('cpu')
        for head, bias in (('sigmoid', math.log(4)), ('tanh', math.atanh(.6))):
            agent.model = Policy(config=ModelConfig(hidden=16, history_layers=0,
                                                   history_heads=2, value_head=head)).eval()
            with torch.no_grad():
                agent.model.critic.weight.zero_()
                agent.model.critic.bias.fill_(bias)
            self.assertAlmostEqual(agent.predict_score(arena, 0), .8, places=6)
            self.assertTrue(all(parameter.grad is None for parameter in agent.model.parameters()))

        class PerspectiveModel(torch.nn.Module):
            def forward(self, states):
                return None, .2 + .6 * states[:, 3] * 10

            def signed_values(self, values):
                return 2 * values - 1

        agent.model = PerspectiveModel()
        arena.reset_play(0, 1, 1.0)
        before = arena.observe()[0].copy()
        self.assertAlmostEqual(agent.predict_score(arena, 0), .2, places=6)
        self.assertAlmostEqual(agent.predict_score(arena, 1), .8, places=6)
        np.testing.assert_array_equal(before, arena.observe()[0])
        with self.assertRaises(ValueError):
            agent.predict_score(arena, 2)

    def test_terminal_status_retained_only_when_requested(self):
        arena = engine.Arena(1, 1)
        iron = play.parse_action('iron')
        moves = np.array([[iron, iron]], np.int32)
        for auto_reset in (False, True):
            arena.reset(0, [[iron, iron]] * (engine.ROUND_LIMIT - 1))
            self.assertEqual(arena.step(moves, reset_finished=auto_reset)[0, 0], 4)
            self.assertEqual(arena.status()['round'], 1 if auto_reset else 101)
            self.assertEqual(arena.status()['outcome'], 0 if auto_reset else 4)
            if not auto_reset:
                with self.assertRaises(ValueError):
                    arena.step(moves)
                arena.reset_play(0, 1, .5)
                self.assertEqual(arena.status()['players'][1]['iron'], .5)
        arena.reset(0, [[iron, iron]] * 10)
        for _ in range(10):
            result = arena.step(np.array([[play.parse_action('stick'), iron]], np.int32),
                                reset_finished=False)[0]
        self.assertEqual(result[0], 1)
        self.assertEqual(arena.status()['players'][1]['hp'], 0)

    def test_one_match_only_and_review_prompt_after_resign_or_terminal(self):
        class Agent:
            last_search = None

            def predict_score(self, arena, seat):
                return .625

            def choose(self, arena, seat):
                return engine.ACTION_NAMES.index('iron 0')

        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / 'master-01.pt').touch()
            parser = argparse.ArgumentParser()
            play.configure(parser)
            args = parser.parse_args(['--checkpoint-dir', folder, '--seed', '42'])
            for inputs in (['1', 'invalid', '0', 'iron', 'resign', 'n'],
                           ['1'] + ['iron'] * engine.ROUND_LIMIT + ['n']):
                output = io.StringIO()
                with patch('master.play.Agent', return_value=Agent()) as constructor, \
                     patch('builtins.input', side_effect=inputs), contextlib.redirect_stdout(output):
                    play.run(args)
                self.assertEqual(constructor.call_count, 1)
                rendered = output.getvalue()
                self.assertIn('Player: 0.5 iron | Master: 0 iron', rendered)
                self.assertEqual(rendered.count('Game start |'), 1)
                self.assertIn('HP 10/10', rendered)
                self.assertNotIn('0: iron 0', rendered)
                self.assertNotIn('Action ID', rendered)
                self.assertIn('Master win estimate: 62.5%', rendered)
                self.assertIn('enter resign to concede (认输)', rendered)

    def test_invalid_move_and_removed_new_command_keep_ai_commitment(self):
        observed = []

        class Agent:
            last_search = None

            def predict_score(self, arena, seat):
                return .625

            def choose(self, arena, seat):
                status = arena.status()
                observed.append((status['round'], status['players'][1-seat]['iron'],
                                 status['players'][seat]['iron']))
                return play.parse_action('iron')

        with tempfile.TemporaryDirectory() as folder:
            (Path(folder) / 'master-01.pt').touch()
            parser = argparse.ArgumentParser()
            play.configure(parser)
            args = parser.parse_args(['--checkpoint-dir', folder, '--starting-iron', '1'])
            with patch('master.play.Agent', return_value=Agent()), \
                 patch('builtins.input', side_effect=['magicattack 1', 'new', 'iron', 'resign', 'n']), \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                play.run(args)
            self.assertEqual(observed, [(1, 1, 0), (2, 2, 1)])
            self.assertIn('Rejected:', output.getvalue())
            self.assertIn('Unknown skill name.', output.getvalue())

    def test_mode_menu_defaults_to_zero_and_fixed_zero_skips_menu(self):
        for entry, expected in (('', 0), ('0', 0), ('1', .5), ('2', 1), ('q', None)):
            with patch('builtins.input', return_value=entry), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(play.starting_iron(None), expected)
        with patch('builtins.input') as prompt:
            self.assertEqual(play.starting_iron(0), 0)
            prompt.assert_not_called()

        class Agent:
            last_search = None

            def predict_score(self, arena, seat):
                self_status = arena.status()['players'][seat]
                if self_status['iron'] != 0:
                    raise AssertionError('Master must start at zero')
                return .5

            def choose(self, arena, seat):
                return play.parse_action('iron')

        parser = argparse.ArgumentParser()
        play.configure(parser)
        args = parser.parse_args(['--seed', '42'])
        with patch('master.play.Agent', return_value=Agent()), \
             patch('builtins.input', side_effect=['', 'resign', 'n']), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            play.run(args)
        self.assertEqual(output.getvalue().count('Player: 0 iron | Master: 0 iron'), 1)

    def test_timeline_reconstructs_from_opening_including_history_and_terminal(self):
        iron, stick = play.parse_action('iron'), play.parse_action('stick')
        record = play.GameRecord(.5)
        arena = record.state_at(0, 1)
        saved = [arena.observe()[0].copy()]
        for actions in [(iron, iron)] * 10 + [(stick, iron)] * 10:
            record.moves.append(actions)
            arena.step(np.array([actions], np.int32), reset_finished=False)
            saved.append(arena.observe()[0].copy())
        for position in (20, 19, 5, 0, 11, 1, 20):
            restored = record.state_at(position, 1)
            np.testing.assert_array_equal(restored.observe()[0], saved[position])
            self.assertEqual(restored.metadata()[0, 0], position + 1)
        self.assertEqual(record.state_at(20, 1).status()['outcome'], 1)
        with self.assertRaises(ValueError):
            record.state_at(21, 1)

    def test_review_boundaries_recommendations_and_trial_restore_original_anchor(self):
        iron = play.parse_action('iron')
        record = play.GameRecord(.5, moves=[(iron, iron)] * 2, ending='Player resigns. Master wins.')
        original = list(record.moves)
        trial_rounds, recommendation_rounds = [], []

        class Agent:
            last_search = None

            def predict_score(self, arena, seat):
                return .5

            def choose(self, arena, seat):
                trial_rounds.append(arena.status()['round'])
                return iron

            def analyze(self, arena):
                recommendation_rounds.append(arena.status()['round'])
                _, masks = arena.observe()
                actions = [np.flatnonzero(masks[0, seat])[:3] for seat in range(2)]
                choices = np.full((1, 2, 3), -1, np.int32)
                policy = np.zeros((2, engine.SKILL_NUM), np.float32)
                for seat in range(2):
                    choices[0, seat, :len(actions[seat])] = actions[seat]
                    policy[seat, actions[seat]] = 1 / len(actions[seat])
                return policy, SearchInfo(2, completed_depth=2, root_candidates=choices)

        inputs = ['2', 'prev', '2', 'next', '1', '3', 'iron', '2', '4', '1',
                  '2', 'next', '2', 'next', '2', 'prev', 'q']
        displayed = []
        actual_print_status = play.print_status

        def capture(arena, human_seat, finished=False):
            displayed.append(arena.observe()[0].copy())
            actual_print_status(arena, human_seat, finished)

        with patch('builtins.input', side_effect=inputs), \
             patch('master.play.print_status', side_effect=capture), \
             contextlib.redirect_stdout(io.StringIO()) as output:
            play.replay_game(record, Agent(), 1)
        rendered = output.getvalue()
        self.assertIn('At position 0; only next is available.', rendered)
        self.assertIn('At the last position; only prev is available.', rendered)
        self.assertIn('Review navigation is disabled during trial.', rendered)
        self.assertIn('Returned to trial starting position 1.', rendered)
        self.assertIn('Player top 3:', rendered)
        self.assertIn('Master top 3:', rendered)
        self.assertIn('Master win estimate: 50.0%', rendered)
        self.assertEqual(trial_rounds, [2, 3])
        self.assertEqual(recommendation_rounds, [2])  # Same position uses cached analysis.
        self.assertEqual(record.moves, original)
        # The last review state is the anchor, with the original one-round history.
        np.testing.assert_array_equal(displayed[-1], record.state_at(1, 1).observe()[0])

    def test_analysis_is_deterministic_legal_and_does_not_change_live_rng_or_state(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'model.pt'
            model = Policy(config=ModelConfig(hidden=16, history_layers=0, history_heads=2))
            from master.models import checkpoint
            torch.save(checkpoint(model), path)
            agent = MasterAgent(path, seed=42, width=2, leaf_width=1, iterations=16,
                                search_depth=2, max_nodes=500)
            arena = engine.Arena(1, 1)
            arena.reset_play(0, 0, .5)
            before = arena.observe()[0].copy()
            rng_before = repr(agent.rng.bit_generator.state)
            policies, info = agent.analyze(arena)
            again, _ = agent.analyze(arena)
            np.testing.assert_array_equal(policies, again)
            np.testing.assert_array_equal(arena.observe()[0], before)
            self.assertEqual(repr(agent.rng.bit_generator.state), rng_before)
            masks = arena.observe()[1][0]
            for seat in range(2):
                choices = info.root_candidates[0, seat]
                choices = choices[choices >= 0]
                self.assertEqual(len(choices), min(3, int(masks[seat].sum())))
                self.assertTrue(masks[seat, choices].all())
                self.assertTrue((policies[seat, masks[seat] == 0] == 0).all())

    def test_terminal_review_cannot_start_trial_or_recommend_moves(self):
        iron = play.parse_action('iron')
        record = play.GameRecord(0, moves=[(iron, iron)] * engine.ROUND_LIMIT)
        arena = record.state_at(engine.ROUND_LIMIT, 1)
        with patch('builtins.input') as prompt, contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertFalse(play.try_position(arena, None, 0))
            self.assertIsNone(play.recommendations(arena, None, 0))
        prompt.assert_not_called()
        self.assertIn('terminal position', output.getvalue())

    def test_resign_opens_review_with_original_model_and_record(self):
        class Agent:
            last_search = None

            def predict_score(self, arena, seat):
                return .5

            def choose(self, arena, seat):
                return play.parse_action('iron')

        agent = Agent()
        parser = argparse.ArgumentParser()
        play.configure(parser)
        args = parser.parse_args(['--starting-iron', '0.5'])
        with patch('master.play.Agent', return_value=agent) as constructor, \
             patch('builtins.input', side_effect=['iron', 'resign', 'yes']), \
             patch('master.play.replay_game') as review, contextlib.redirect_stdout(io.StringIO()):
            play.run(args)
        constructor.assert_called_once()
        record, reviewed_agent, threads = review.call_args.args
        self.assertIs(reviewed_agent, agent)
        self.assertEqual(record.iron, .5)
        self.assertEqual(record.moves, [(play.parse_action('iron'), play.parse_action('iron'))])
        self.assertEqual(record.ending, 'Player resigns. Master wins.')
        self.assertEqual(threads, args.threads)

    def test_trial_resignation_and_terminal_return_without_modifying_record(self):
        iron, stick = play.parse_action('iron'), play.parse_action('stick')

        class Agent:
            last_search = None

            def predict_score(self, arena, seat):
                return .5

            def choose(self, arena, seat):
                return iron

        record = play.GameRecord(.5, moves=[(iron, iron)] * 10 + [(stick, iron)] * 9)
        before = record.state_at(19, 1).observe()[0].copy()
        for commands in (['resign'], ['stick']):
            with patch('builtins.input', side_effect=commands), contextlib.redirect_stdout(io.StringIO()):
                self.assertFalse(play.try_position(record.state_at(19, 1), Agent(), 0))
            np.testing.assert_array_equal(record.state_at(19, 1).observe()[0], before)
            self.assertEqual(len(record.moves), 19)


if __name__ == '__main__':
    unittest.main()
