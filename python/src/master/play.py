"""A command-line match; the AI commits before human input."""
import argparse

import numpy as np
import torch
from blacksmith import master as engine

from .agent import Agent


def run(args: argparse.Namespace) -> None:
    torch.set_num_threads(args.torch_threads)
    agent = Agent(args.checkpoint, args.device, args.seed, args.width,
                  args.leaf_width, args.regret_iterations)
    arena = engine.Arena(1, args.threads)
    ai_seat = 1
    while True:
        chosen = agent.choose(arena, ai_seat)
        observations, masks = arena.observe()
        view = observations[0, 1 - ai_seat]
        print(f"Round {arena.metadata()[0, 0]} | HP {view[0] * 10:g} / "
              f"{view[engine.COMMON_ENCODING_LENGTH] * 10:g}")
        legal = np.flatnonzero(masks[0, 1 - ai_seat])
        print("\n".join(f"{index}: {engine.ACTION_NAMES[index]}" for index in legal))
        while True:
            try:
                text = input("Action ID, new, or q: ").strip()
            except (EOFError, KeyboardInterrupt):
                return
            if text == "q":
                return
            if text == "new":
                arena.reset(0)
                ai_seat = 1 - ai_seat
                break
            if not text.isdecimal() or int(text) not in legal:
                print("Choose a legal action ID.")
                continue
            actions = np.zeros((1, 2), np.int32)
            actions[0, ai_seat], actions[0, 1 - ai_seat] = chosen, int(text)
            result = arena.step(actions)[0]
            print(f"AI: {engine.ACTION_NAMES[chosen]} | You: {engine.ACTION_NAMES[int(text)]}")
            if result[0]:
                outcome = int(result[0])
                print("Draw" if outcome >= 3 else "AI wins" if outcome == ai_seat + 1 else "You win")
                ai_seat = 1 - ai_seat
            break


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--checkpoint", required=True)
    parser.add_argument("--device", default="cpu")
    for name, default in (("seed", 42), ("width", 24), ("leaf-width", 12),
                          ("regret-iterations", 128), ("threads", 6), ("torch-threads", 4)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.set_defaults(func=run)
