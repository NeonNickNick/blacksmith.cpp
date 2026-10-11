"""Balanced-seat evaluation of frozen master checkpoints."""
import argparse
import json
from pathlib import Path
import time

import numpy as np
import torch
from blacksmith import master as engine

from .agent import Agent
from .runtime import digest, settings, write_json


def run(args: argparse.Namespace) -> None:
    if args.games < 2 or args.games % 2:
        raise ValueError("Games must be positive and even")
    torch.set_num_threads(args.torch_threads)
    first = Agent(args.first, args.device, args.seed, args.width, args.leaf_width, args.regret_iterations)
    second = Agent(args.second, args.device, args.seed + 1, args.width, args.leaf_width, args.regret_iterations)
    arena = engine.Arena(1, args.threads)
    records: list[dict[str, int]] = []
    start = time.monotonic()
    for game in range(args.games):
        seat = game % 2
        while True:
            actions = np.zeros((1, 2), np.int32)
            # Both agents decide on the same state before either action is submitted.
            actions[0, seat] = first.choose(arena, seat)
            actions[0, 1 - seat] = second.choose(arena, 1 - seat)
            outcome, rounds = arena.step(actions)[0, :2].astype(int)
            if outcome:
                result = 0 if outcome >= 3 else 1 if outcome == seat + 1 else -1
                record = {"game": game + 1, "seat": seat, "result": result,
                          "outcome": int(outcome), "rounds": int(rounds)}
                records.append(record)
                print(json.dumps(record), flush=True)
                break
    wins = sum(record["result"] == 1 for record in records)
    losses = sum(record["result"] == -1 for record in records)
    report = {"games": args.games, "wins": wins, "losses": losses,
              "draws": args.games - wins - losses, "win_rate": wins / args.games,
              "seconds": time.monotonic() - start, "settings": settings(args),
              "first_sha256": digest(args.first), "second_sha256": digest(args.second),
              "rules_sha256": digest(engine.__file__), "results": records}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        raise FileExistsError(output)
    write_json(output, report)


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--first", required=True)
    parser.add_argument("--second", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--device", default="cpu")
    for name, default in (("games", 200), ("seed", 42), ("width", 24), ("leaf-width", 12),
                          ("regret-iterations", 128), ("threads", 6), ("torch-threads", 4)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.set_defaults(func=run)
