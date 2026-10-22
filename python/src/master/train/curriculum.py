"""Build legal curriculum prefixes with batched native simulation."""
import argparse
from dataclasses import dataclass
import json
from pathlib import Path

import numpy as np
from blacksmith import master as engine

from master.models.data import Prefix
from .sampling import COURSE_PAIRS

# Role IDs are part of the engine metadata protocol; skill IDs come from ACTIONS.
ACQUISITIONS = tuple(engine.ROLES)


@dataclass(frozen=True)
class Entry:
    roles: tuple[int, int]
    prefix: Prefix


def action_named(name: str) -> int:
    return engine.parse_action(name)


def build(per_pair: int = 64, seed: int = 42, threads: int = 1) -> list[Entry]:
    if per_pair < 1 or threads < 1:
        raise ValueError("per_pair and threads must be positive")
    rng = np.random.default_rng(seed)
    iron = action_named("IRON")
    acquisition = [iron, *(action_named(name) for name in ACQUISITIONS[1:])]
    banned = set(acquisition[1:])
    allowed = np.array([index not in banned for index in range(engine.SKILL_NUM)])
    entries: list[Entry] = []
    for first, second in COURSE_PAIRS:
        seen: set[bytes] = set()
        made = attempts = 0
        while made < per_pair and attempts < per_pair * 200:
            count = min(128, max(4, 2 * (per_pair - made)))
            attempts += count
            arena = engine.Arena(count, threads)
            waits = rng.integers(0, 5, size=(count, 2))
            extra = rng.integers(0, 17, size=count)
            end = np.full(count, -1, np.int32)
            prefixes: list[Prefix] = [[] for _ in range(count)]
            live = np.ones(count, bool)
            for turn in range(40):
                observations, masks = arena.observe()
                current = arena.metadata()[:, 1:]
                prepared = (current == (first, second)).all(-1) & (turn >= waits.max(-1))
                beginning = live & prepared & (end < 0)
                end[beginning] = turn + extra[beginning]
                finished = live & (end == turn)
                # DreamWake can restore a pre-profession state during opening construction.
                complete = finished & (current == (first, second)).all(-1)
                for env in np.flatnonzero(complete):
                    key = observations[env, 0].tobytes()
                    if key not in seen and made < per_pair:
                        seen.add(key)
                        entries.append(Entry((first, second), prefixes[env].copy()))
                        made += 1
                live[finished] = False
                if not live.any() or made == per_pair:
                    break
                choices = np.full((count, 2), iron, np.int32)
                for env in np.flatnonzero(live):
                    for seat, target in enumerate((first, second)):
                        if end[env] >= 0:
                            legal = np.flatnonzero((masks[env, seat] > 0) & allowed)
                            choices[env, seat] = rng.choice(legal)
                        elif current[env, seat] != target and turn >= waits[env, seat]:
                            desired = acquisition[target]
                            if masks[env, seat, desired]:
                                choices[env, seat] = desired
                    prefixes[env].append(choices[env].tolist())
                results = arena.step(choices)
                live &= results[:, 0] == 0
            if attempts >= per_pair * 200 and made < per_pair:
                raise RuntimeError(f"Could not generate enough starts for {(first, second)}")
    return entries


def run(args: argparse.Namespace) -> None:
    entries = build(args.per_pair, args.seed, args.threads)
    payload = {"schema_version": engine.SCHEMA_VERSION, "seed": args.seed,
               "roles": engine.ROLES, "per_pair": args.per_pair,
               "entries": [{"roles": entry.roles, "prefix": entry.prefix} for entry in entries]}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as handle:
        json.dump(payload, handle, separators=(",", ":"))
    print(json.dumps({"output": str(output), "entries": len(entries)}), flush=True)


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output", required=True)
    parser.add_argument("--per-pair", type=int, default=64)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--threads", type=int, default=1)
    parser.set_defaults(func=run)
