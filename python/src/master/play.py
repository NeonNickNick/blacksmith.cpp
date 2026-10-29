"""A command-line match; the AI commits before human input."""
# 单局对战、原局复盘和临时试玩共用规则引擎，但只有正式对局写入 GameRecord。
import argparse
from dataclasses import dataclass, field
from pathlib import Path
import random

import numpy as np
import torch
from blacksmith import master as engine

from .agent import Agent

DEFAULT_CHECKPOINT_DIR = Path(__file__).resolve().parents[3] / 'models' / 'blacksmith-master'
DEFAULT_CHECKPOINT = DEFAULT_CHECKPOINT_DIR / 'master-0099.pt'

def parse_action(text: str) -> int:
    return engine.parse_action(text)


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
        resources = ('cross', 'dream', 'spirit', 'fire', 'water', 'wood', 'earth', 'bolt')
        print('  ' + ' | '.join(f'{key.title()}: {player[key]:g}' for key in resources))


class CheckpointPool:
    def __init__(self, checkpoint=None, directory=None, seed=None):
        self.paths = (sorted(Path(directory).glob('*.pt')) if directory
                      else [Path(checkpoint) if checkpoint else DEFAULT_CHECKPOINT])
        if not self.paths or any(not path.is_file() for path in self.paths):
            raise FileNotFoundError('No play checkpoints found; set --checkpoint or --checkpoint-dir')
        self.rng = random.Random(seed)

    def next(self):
        return self.rng.choice(self.paths), self.rng.getrandbits(32)


@dataclass
class GameRecord:
    iron: float
    human_seat: int = 0
    moves: list[tuple[int, int]] = field(default_factory=list)
    ending: str = ''

    def state_at(self, position: int, threads: int):
        if not 0 <= position <= len(self.moves):
            raise ValueError('Review position outside the recorded game')
        arena = engine.Arena(1, threads)
        arena.reset_play(0, self.human_seat, self.iron)
        # position=N 表示已经完成 N 回合，0 是保留原始铁量模式的开局。
        # There is no undo: reconstruct the complete rules state and history.
        for actions in self.moves[:position]:
            arena.step(np.array([actions], np.int32), reset_finished=False)
        return arena


def read_text(prompt: str) -> str | None:
    try:
        return input(prompt).strip().lower()
    except (EOFError, KeyboardInterrupt):
        return None


def result_text(outcome: int, human_seat: int) -> str:
    if outcome >= 3:
        return 'Draw (round limit)' if outcome == 4 else 'Draw'
    return 'Player wins' if outcome == human_seat + 1 else 'Master wins'


def print_prediction(arena, agent, human_seat: int) -> None:
    print(f'Master win estimate: {agent.predict_score(arena, 1 - human_seat):.1%} '
          '(value head; draw = 50%)')


def play_round(arena, agent, human_seat: int, trial=False):
    ai_seat = 1 - human_seat
    print_status(arena, human_seat)
    print_prediction(arena, agent, human_seat)
    chosen = agent.choose(arena, ai_seat)
    # Master 在玩家输入前承诺动作；无效输入只重问玩家，不能让 Master 重新抽样。
    if agent.last_search:
        info = agent.last_search
        print(f'Search depth {info.completed_depth}/{info.requested_depth} | '
              f'nodes {info.expanded_nodes}' + (' | node budget reached' if info.budget_exhausted else ''))
    _, masks = arena.observe()
    while True:
        text = read_text('Trial player > ' if trial else 'Player > ')
        if text is None or text == 'q':
            return 'quit', None, 0
        if text in ('resign', 'surrender', '认输'):
            return 'resign', None, 0
        if trial and text in ('4', 'back'):
            return 'back', None, 0
        if trial and text in ('1', '2', 'prev', 'next', 'p', 'n'):
            print('Review navigation is disabled during trial. Use 4 to return to its starting position.')
            continue
        try:
            action = parse_action(text)
        except ValueError as error:
            print(error)
            continue
        if not masks[0, human_seat, action]:
            print('Rejected: skill unavailable for your current resources or profession.')
            continue
        actions = [0, 0]
        actions[ai_seat], actions[human_seat] = chosen, action
        result = arena.step(np.array([actions], np.int32), reset_finished=False)[0]
        print(f'Player: {engine.ACTION_NAMES[action]}\nMaster: {engine.ACTION_NAMES[chosen]}')
        return 'move', tuple(actions), int(result[0])


def recommendations(arena, agent, human_seat: int, cached=None):
    if arena.status(0)['outcome']:
        print('Terminal position: there is no next move to recommend.')
        return None
    try:
        policy, info = agent.analyze(arena) if cached is None else cached
    except (ValueError, RuntimeError) as error:
        print(f'Analysis unavailable: {error}')
        return None
    print(f'Top searched actions | depth {info.completed_depth}/{info.requested_depth} '
          f'| nodes {info.expanded_nodes} | weights are search-policy weights')
    for label, seat in (('Player', human_seat), ('Master', 1 - human_seat)):
        # 只在真正被根搜索考察的动作中排序；策略权重不是该动作的单独胜率。
        candidates = set(int(action) for action in info.root_candidates[0, seat] if action >= 0)
        ranked = sorted(candidates, key=lambda action: (-float(policy[seat, action]), action))[:3]
        print(f'{label} top {len(ranked)}:')
        for rank, action in enumerate(ranked, 1):
            print(f'  {rank}. {engine.ACTION_NAMES[action]} | {policy[seat, action]:.1%}')
    return policy, info


def try_position(arena, agent, human_seat: int) -> bool:
    """Return True to leave play entirely, False to return to the review anchor."""
    if arena.status(0)['outcome']:
        print('Cannot start a trial from a terminal position.')
        return False
    print('Trial: enter skill param [skill param ...]; 4/back returns to the trial start; resign ends the trial; q exits.')
    # 此处直接推进重建出的临时 Arena，不添加原局动作，也不提供前后导航。
    while True:
        command, _, outcome = play_round(arena, agent, human_seat, trial=True)
        if command == 'quit':
            return True
        if command in ('back', 'resign'):
            if command == 'resign':
                print('Trial: Player resigns. Master wins.')
            return False
        if outcome:
            print_status(arena, human_seat, finished=True)
            print(f'Trial: {result_text(outcome, human_seat)}')
            return False


def replay_game(record: GameRecord, agent, threads: int) -> None:
    position = 0
    # 每个复盘位置的推荐缓存独立；同一位置重复查看不会重新消耗搜索。
    analyses = {}
    print(f'Recorded game: {record.ending}')
    while True:
        arena = record.state_at(position, threads)
        print(f'\nReview position {position}/{len(record.moves)} (after {position} completed rounds)')
        if position:
            actions = record.moves[position - 1]
            print(f'Last round | Player: {engine.ACTION_NAMES[actions[record.human_seat]]} | '
                  f'Master: {engine.ACTION_NAMES[actions[1 - record.human_seat]]}')
        print_status(arena, record.human_seat, finished=bool(arena.status(0)['outcome']))
        print_prediction(arena, agent, record.human_seat)
        print('1 = recommendations; 2 = move one round (prev/next); 3 = trial; '
              '4 = return from trial (trial only); q = exit')
        command = read_text('Review > ')
        if command is None or command == 'q':
            return
        if command == '1':
            analysis = recommendations(arena, agent, record.human_seat, analyses.get(position))
            if analysis is not None:
                analyses[position] = analysis
        elif command == '2' or command in ('prev', 'next', 'p', 'n'):
            if not record.moves:
                print('No recorded moves to traverse.')
                continue
            directions = ['prev'] if position else []
            if position < len(record.moves):
                directions.append('next')
            direction = read_text(f'Move [{"/".join(directions)}] > ') if command == '2' else command
            if direction is None or direction == 'q':
                return
            if direction in ('prev', 'p'):
                if position > 0:
                    position -= 1
                else:
                    print('At position 0; only next is available.')
            elif direction in ('next', 'n'):
                if position < len(record.moves):
                    position += 1
                else:
                    print('At the last position; only prev is available.')
            else:
                print('Use prev or next to move exactly one round.')
        elif command == '3':
            # 返回时 position 不变；下一轮从记录重建，试玩中的状态变化全部丢弃。
            if try_position(arena, agent, record.human_seat):
                return
            print(f'Returned to trial starting position {position}.')
        elif command == '4':
            print('Already reviewing; 4 is used inside a trial.')
        else:
            print('Choose 1, 2, 3, or q.')


def run(args: argparse.Namespace) -> None:
    if (min(args.threads, args.torch_threads, args.width, args.leaf_width,
            args.search_depth, args.search_nodes, args.search_batch, args.regret_iterations) <= 0
            or max(args.width, args.leaf_width) > engine.SKILL_NUM
            or args.search_depth > engine.ROUND_LIMIT):
        raise ValueError(f'Positive play limits required; widths <={engine.SKILL_NUM}, depth <={engine.ROUND_LIMIT}')
    torch.set_num_threads(args.torch_threads)
    print('During the game, enter resign to concede (认输); q exits the program.')
    pool = CheckpointPool(args.checkpoint, args.checkpoint_dir, args.seed)

    iron = starting_iron(args.starting_iron)
    if iron is None:
        return
    record = GameRecord(iron)
    arena = record.state_at(0, args.threads)
    path, seed = pool.next()
    # 一局只选一次权重，后续复盘/试玩始终使用同一个 Agent。
    print(f'\nGame start | Model: {path.name} | Player: {iron:g} iron | Master: 0 iron')
    print('Enter skill name + parameter; omitted parameter means 0.')
    agent = Agent(path, args.device, seed, args.width, args.leaf_width,
                  args.regret_iterations, args.search_depth, args.search_nodes, args.search_batch)
    while True:
        command, actions, outcome = play_round(arena, agent, record.human_seat)
        if command == 'quit':
            return
        if command == 'resign':
            record.ending = 'Player resigns. Master wins.'
            break
        record.moves.append(actions)
        if outcome:
            print_status(arena, record.human_seat, finished=True)
            record.ending = result_text(outcome, record.human_seat)
            break
    print(record.ending)
    while True:
        answer = read_text('Replay the game? [y/N] > ')
        # 终局只询问是否复盘；无论选项如何，都不会自动启动另一局。
        if answer is None or answer in ('', 'n', 'no', 'q'):
            return
        if answer in ('y', 'yes'):
            replay_game(record, agent, args.threads)
            return
        print('Use y to review, or n to exit.')


def configure(parser: argparse.ArgumentParser) -> None:
    checkpoints = parser.add_mutually_exclusive_group()
    checkpoints.add_argument("--checkpoint", help="Use a fixed model (default: master-0099.pt)")
    checkpoints.add_argument("--checkpoint-dir", help="Randomly choose a .pt file from this directory each game")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--seed", type=int, default=None, help="Optional reproducible action and model sampling")
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
