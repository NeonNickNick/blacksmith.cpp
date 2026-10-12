"""Blacksmith neural master: search-guided self-play and local inference."""
import argparse


def main() -> None:
    from .train import curriculum, train
    from . import play

    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, module in (("curriculum", curriculum), ("train", train), ("play", play)):
        module.configure(commands.add_parser(name, description=module.__doc__))
    args = parser.parse_args()
    args.func(args)
