"""Build legal curriculum prefixes with batched native simulation."""
# 用真实规则推进到指定职业/资源状态，训练从这些状态开始，前缀本身不作为标签。
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
    # roles 是前缀结束时的实际职业；prefix 足以在新 Arena 中重建完整状态和历史。
    roles: tuple[int, int]
    prefix: Prefix


def action_named(name: str) -> int:
    return engine.parse_action(name)


def build(per_pair: int = 64, seed: int = 42, threads: int = 1,
          focus_role: str | None = None, association_ready: bool = False) -> list[Entry]:
    if per_pair < 1 or threads < 1:
        raise ValueError("per_pair and threads must be positive")
    rng = np.random.default_rng(seed)
    iron = action_named("IRON")
    phantom = engine.ROLES.index("phantombook")
    fantasia = action_named("fantasia")
    if association_ready and focus_role != "phantombook":
        raise ValueError("Association-ready starts require --focus-role phantombook")
    acquisition = [iron, *(action_named(name) for name in ACQUISITIONS[1:])]
    banned = set(acquisition[1:])
    allowed = np.array([index not in banned for index in range(engine.SKILL_NUM)])
    entries: list[Entry] = []
    for first, second in COURSE_PAIRS:
        # 职业对有序，幻影书在第一席和第二席的情况分别生成。
        if focus_role is not None and engine.ROLES.index(focus_role) not in (first, second):
            continue
        if association_ready and first == second:
            continue
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
                # 获得职业后再随机推进若干回合，避免所有课程只有同一个资源状态。
                end[beginning] = turn + extra[beginning]
                finished = live & (end == turn)
                # DreamWake can restore a pre-profession state during opening construction.
                complete = finished & (current == (first, second)).all(-1)
                borrowed = (masks[..., engine.BASE_ACTION_COUNT:] > 0).any(-1)
                if association_ready:
                    ready = ((current == phantom) & borrowed).any(-1)
                    # 已到计划终点但还不能联想时继续准备，而不是接受无效专项起点。
                    end[finished & ~ready] = turn + 1
                    live[finished & ~ready] = True
                    complete &= ready
                for env in np.flatnonzero(complete):
                    # 编码包含实际状态与历史；同一职业对内不重复收录相同输入。
                    key = observations[env, 0].tobytes()
                    if key not in seen and made < per_pair:
                        seen.add(key)
                        entries.append(Entry((first, second), prefixes[env].copy()))
                        made += 1
                live[finished & (end == turn)] = False
                if not live.any() or made == per_pair:
                    break
                choices = np.full((count, 2), iron, np.int32)
                for env in np.flatnonzero(live):
                    for seat, target in enumerate((first, second)):
                        if end[env] >= 0:
                            if association_ready and current[env, seat] == phantom and not borrowed[env, seat]:
                                # Fantasia 积累 Dream；是否能借用某招仍由原生掩码决定。
                                choices[env, seat] = fantasia if masks[env, seat, fantasia] else iron
                            else:
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
    entries = build(args.per_pair, args.seed, args.threads, args.focus_role, args.association_ready)
    payload = {"schema_version": engine.SCHEMA_VERSION, "seed": args.seed,
               "roles": engine.ROLES, "per_pair": args.per_pair,
               "focus_role": args.focus_role, "association_ready": args.association_ready,
               "entries": [{"roles": entry.roles, "prefix": entry.prefix} for entry in entries]}
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf-8") as handle:
        # 课程库不可静默覆盖，防止同一路径下的训练输入被替换。
        json.dump(payload, handle, separators=(",", ":"))
    print(json.dumps({"output": str(output), "entries": len(entries)}), flush=True)


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--output", required=True)
    parser.add_argument("--per-pair", type=int, default=64)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--threads", type=int, default=1)
    parser.add_argument("--focus-role", choices=engine.ROLES[1:])
    parser.add_argument("--association-ready", action="store_true")
    parser.set_defaults(func=run)
