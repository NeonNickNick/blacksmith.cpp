"""A command-line match; the AI commits before human input."""
import argparse
from pathlib import Path
import random

import numpy as np
import torch
from blacksmith import master as engine

from .agent import Agent

DEFAULT_CHECKPOINT_DIR = Path(__file__).resolve().parents[3] / 'models' / 'blacksmith-master'

# Names accepted by standard_pvp, mapped to the domain skill enum explicitly.
SKILLS = {
    'iron': 0, 'stick': 1, 'drill': 2, 'slash': 3, 'tear': 4,
    'shield': 5, 'thornshield': 6, 'recovery': 7, 'space': 8,
    'time': 9, 'reflect': 10, 'delayprotection': 11, 'armor12': 12,
    'warlock': 13, 'alchemy': 14, 'cannon': 15, 'driver': 16,
    'lancer': 17, 'bloodsigil': 18, 'magic': 19, 'magicattack': 20,
    'magicshield': 21, 'sacrifice': 22, 'mute': 23, 'midastouch': 24,
    'strike': 25, 'doublestrike': 26, 'triplestrike': 27, 'apshell': 28,
    'cannonbarrier': 29, 'spaceattack': 30, 'time2space': 31,
    'space2time': 32, 'spacebarrier': 33, 'skystrike': 34,
    'tyrantdestruction': 35, 'dragontooth': 36, 'triplestab': 37,
    'charge': 38, 'risingdragon': 39, 'bloodblade': 40,
    'bloodlust': 41, 'bloodrecovery': 42, 'bloodrage': 43,
}


def parse_action(text: str) -> int:
    parts = text.lower().split()
    if len(parts) not in (1, 2) or (len(parts) == 2 and not parts[1].isascii()):
        raise ValueError('Use skill name + nonnegative integer parameter.')
    if parts[0] not in SKILLS:
        raise ValueError('Unknown skill name.')
    if len(parts) == 2 and not parts[1].isdecimal():
        raise ValueError('Use skill name + nonnegative integer parameter.')
    parameter = int(parts[1]) if len(parts) == 2 else 0
    if parameter > 5:
        raise ValueError('Skill parameters must be between 0 and 5.')
    try:
        return engine.action_index(SKILLS[parts[0]], parameter)
    except ValueError:
        raise ValueError('This skill does not support that parameter.') from None


def starting_iron(fixed: float | None) -> float | None:
    if fixed is not None:
        return fixed
    print('\nStarting mode: 0 = Both 0 iron (default); 1 = Player 0.5 iron; 2 = Player 1 iron.')
    print('Master starts with 0 iron in all modes.')
    while True:
        try:
            text = input('Mode [0/1/2, Enter = 0], or q: ').strip().lower()
        except (EOFError, KeyboardInterrupt):
            return None
        if text == 'q':
            return None
        if text in ('', '0', '1', '2'):
            return {'': 0.0, '0': 0.0, '1': 0.5, '2': 1.0}[text]
        print('Choose mode 0, 1 or 2; Enter selects 0.')


def print_status(arena, human_seat: int, finished=False) -> None:
    status = arena.status(0)
    round_number = status['round'] - int(finished)
    print(f'\n---------- Round {round_number} {"result" if finished else "start"} ----------')
    for label, seat in (('Player', human_seat), ('Master', 1 - human_seat)):
        player = status['players'][seat]
        print(f'{label}: HP {player["hp"]}/{player["mhp"]} | Role: {engine.ROLES[player["role"]]}')
        print(f'  Iron: {player["iron"]:g} | Gold iron: {player["gold_iron"]:g} | '
              f'Magic: {player["magic"]:g} | Space: {player["space"]:g} | Time: {player["time"]:g}')


class CheckpointPool:
    def __init__(self, checkpoint=None, directory=None, seed=None):
        self.paths = [Path(checkpoint)] if checkpoint else sorted(Path(directory or DEFAULT_CHECKPOINT_DIR).glob('*.pt'))
        if not self.paths or any(not path.is_file() for path in self.paths):
            raise FileNotFoundError('No play checkpoints found; set --checkpoint or --checkpoint-dir')
        self.rng = random.Random(seed)

    def next(self):
        return self.rng.choice(self.paths), self.rng.getrandbits(32)


def run(args: argparse.Namespace) -> None:
    if (min(args.threads, args.torch_threads, args.width, args.leaf_width,
            args.search_depth, args.search_nodes, args.search_batch, args.regret_iterations) <= 0
            or max(args.width, args.leaf_width) > engine.SKILL_NUM
            or args.search_depth > engine.ROUND_LIMIT):
        raise ValueError('Positive play limits required; widths <=75, depth <=100')
    torch.set_num_threads(args.torch_threads)
    pool = CheckpointPool(args.checkpoint, args.checkpoint_dir, args.seed)

    arena = engine.Arena(1, args.threads)
    ai_seat = 1

    def new_game():
        iron = starting_iron(args.starting_iron)
        if iron is None:
            return None
        arena.reset_play(0, 1 - ai_seat, iron)
        path, seed = pool.next()
        print(f'\nGame start | Model: {path.name} | Player: {iron:g} iron | Master: 0 iron')
        print('Enter skill name + parameter; omitted parameter means 0. new = new game; q = quit.')
        return Agent(path, args.device, seed, args.width, args.leaf_width,
                     args.regret_iterations, args.search_depth, args.search_nodes, args.search_batch)

    agent = new_game()
    while agent is not None:
        print_status(arena, 1 - ai_seat)
        print(f'Master win estimate: {agent.predict_score(arena, ai_seat):.1%} '
              '(value head; draw = 50%)')
        chosen = agent.choose(arena, ai_seat)
        if agent.last_search:
            info = agent.last_search
            print(f'Search depth {info.completed_depth}/{info.requested_depth} | '
                  f'nodes {info.expanded_nodes}' + (' | node budget reached' if info.budget_exhausted else ''))
        _, masks = arena.observe()
        while True:
            try:
                text = input('Player > ').strip().lower()
            except (EOFError, KeyboardInterrupt):
                return
            if text == "q":
                return
            if text == "new":
                ai_seat = 1 - ai_seat
                agent = new_game()
                break
            try:
                action = parse_action(text)
            except ValueError as error:
                print(error)
                continue
            if not masks[0, 1 - ai_seat, action]:
                print('Rejected: skill unavailable for your current resources or profession.')
                continue
            actions = np.zeros((1, 2), np.int32)
            actions[0, ai_seat], actions[0, 1 - ai_seat] = chosen, action
            # Keep the terminal state for display; explicitly initialise the next game.
            result = arena.step(actions, reset_finished=False)[0]
            print(f'Player: {engine.ACTION_NAMES[action]}\nMaster: {engine.ACTION_NAMES[chosen]}')
            if result[0]:
                print_status(arena, 1 - ai_seat, finished=True)
                outcome = int(result[0])
                print('Draw (round limit)' if outcome == 4 else 'Draw' if outcome == 3
                      else 'Master wins' if outcome == ai_seat + 1 else 'Player wins')
                ai_seat = 1 - ai_seat
                agent = new_game()
            break


def configure(parser: argparse.ArgumentParser) -> None:
    checkpoints = parser.add_mutually_exclusive_group()
    checkpoints.add_argument("--checkpoint", help="Use one fixed model instead of the random pool")
    checkpoints.add_argument("--checkpoint-dir", help="Randomly choose a .pt file from this directory each game")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--seed", type=int, default=None, help="Optional reproducible model and action sampling")
    parser.add_argument('--starting-iron', type=float, choices=(0.0, 0.5, 1.0),
                        help='Player starting iron each game; interactive default is 0. Master always starts at 0.')
    parser.add_argument("--search-depth", "--depth", type=int, default=2,
                        help="Maximum simultaneous rounds to search; iterative deepening within the node budget")
    parser.add_argument("--search-nodes", type=int, default=200000,
                        help="Total expanded joint-action nodes per move across all depths")
    parser.add_argument("--search-batch", type=int, default=4096,
                        help="Approximate maximum joint-action cells in each native expansion batch")
    for name, default in (("width", 24), ("leaf-width", 12),
                          ("regret-iterations", 128), ("threads", 6), ("torch-threads", 4)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.set_defaults(func=run)
