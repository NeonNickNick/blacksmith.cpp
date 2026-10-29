"""Compare a checkpoint's sampled policy or shallow search with deeper search."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import numpy as np
import torch
from blacksmith import master as engine

from .models import load, masked_log_probs
from .search import search


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--checkpoint', required=True)
    parser.add_argument('--games', type=int, default=20)
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--device', default='cpu')
    parser.add_argument('--width', type=int, default=8)
    parser.add_argument('--leaf-width', type=int, default=4)
    parser.add_argument('--depth', type=int, default=2)
    parser.add_argument('--opponent-depth', type=int, default=0,
                        help='First seat search depth; 0 samples the network policy')
    parser.add_argument('--nodes', type=int, default=200000)
    parser.add_argument('--iterations', type=int, default=64)
    parser.add_argument('--threads', type=int, default=1)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    if args.games <= 0 or args.threads <= 0 or args.opponent_depth < 0:
        parser.error('games and threads must be positive; opponent depth must be nonnegative')
    torch.set_num_threads(args.threads)
    device = torch.device(args.device)
    model = load(args.checkpoint).to(device).eval()
    rng_policy = np.random.default_rng(args.seed)
    rng_search = np.random.default_rng(args.seed + 1)
    games = []
    with torch.inference_mode():
        for game in range(args.games):
            arena = engine.Arena(1, args.threads)
            arena.reset(0)
            seconds = [0.0, 0.0]
            depths = []
            opponent_depths = []
            for turn in range(engine.ROUND_LIMIT):
                observation, masks = arena.observe()
                start = time.perf_counter()
                if args.opponent_depth:
                    first_actions, _, first_info = search(model, arena, rng_policy, device, args.width,
                                                          args.leaf_width, args.iterations,
                                                          args.opponent_depth, args.nodes)
                    opponent_depths.append(first_info.completed_depth)
                    first = int(first_actions[0, 0])
                else:
                    logits, _ = model(torch.as_tensor(observation[0, 0:1], device=device))
                    probabilities = masked_log_probs(logits, torch.as_tensor(masks[0, 0:1], device=device))
                    probabilities = probabilities.exp().cpu().numpy()[0].astype(np.float64)
                    probabilities /= probabilities.sum()
                    first = int(rng_policy.choice(engine.SKILL_NUM, p=probabilities))
                seconds[0] += time.perf_counter() - start
                start = time.perf_counter()
                actions, _, info = search(model, arena, rng_search, device, args.width,
                                          args.leaf_width, args.iterations, args.depth, args.nodes)
                seconds[1] += time.perf_counter() - start
                depths.append(info.completed_depth)
                result = arena.step(np.array([[first, int(actions[0, 1])]], dtype=np.int32),
                                    reset_finished=False)[0]
                outcome = int(result[0])
                if outcome:
                    games.append({'game': game, 'outcome': outcome, 'rounds': turn + 1,
                                  'policy_seconds': seconds[0], 'search_seconds': seconds[1],
                                  'completed_depths': depths, 'opponent_completed_depths': opponent_depths})
                    break
            else:
                raise RuntimeError('Game did not terminate at the round limit')
            print(f'game {game + 1}/{args.games}: outcome={outcome}', flush=True)
    results = {'config': vars(args), 'schema_version': engine.SCHEMA_VERSION,
               'checkpoint_sha256': hashlib.sha256(Path(args.checkpoint).read_bytes()).hexdigest(),
               'outcome_codes': {'1': 'first seat wins', '2': 'second seat wins', '3/4': 'draw'},
               'policy_wins': sum(g['outcome'] == 1 for g in games),
               'search_wins': sum(g['outcome'] == 2 for g in games),
               'draws': sum(g['outcome'] >= 3 for g in games), 'games': games}
    Path(args.output).write_text(json.dumps(results, indent=2), encoding='utf-8')


if __name__ == '__main__':
    main()
