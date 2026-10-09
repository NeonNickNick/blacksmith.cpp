"""Generate diverse reachable starts with legal actions only; never edit resources.

The prefixes are scenario preparation, not imitation examples. All restrictions
end before search-guided self-play starts. No human games or trained model is read.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import numpy as np
from arena import ProfessionArena, ROLES


def build(output, per_pair, seed, exclude=None):
    arena = ProfessionArena(1, 1, 4)
    rng = np.random.default_rng(seed)
    iron, warlock, alchemy, cannon, driver, lancer, blood, stick = arena.actions
    acquisitions = {warlock, alchemy, cannon, driver, lancer, blood}
    acquire = {1: warlock, 2: warlock, 3: cannon, 4: driver, 5: lancer}
    entries = []
    seen = {e['observation_sha256'] for e in json.loads(Path(exclude).read_text())['entries']} if exclude else set()
    attempts = 0
    for first in range(6):
        for second in range(6):
            made = 0
            while made < per_pair:
                attempts += 1
                if attempts > per_pair * 36 * 200:
                    raise RuntimeError('Could not construct enough distinct legal scenarios')
                arena.reset(0, [])
                arena.refresh()
                prefix = []
                targets = [first, second]
                waits = rng.integers(0, 5, 2)
                # Randomized acquisition timing, normal resource spending and turn costs.
                for turn in range(24):
                    if turn >= max(waits) and list(arena.info[0, 1:]) == targets:
                        break
                    choices = []
                    for seat, target in enumerate(targets):
                        current = int(arena.info[0, seat+1])
                        desired = alchemy if target == 2 and current == 1 else acquire.get(target)
                        if current != target and turn >= waits[seat] and arena.masks[0, seat, desired]:
                            choices.append(desired)
                        else:
                            choices.append(iron if arena.masks[0, seat, iron] else stick)
                    prefix.append(choices)
                    arena.step(np.array([choices], np.int32))
                    if arena.results[0, 0]:
                        break
                if arena.results[0, 0] or list(arena.info[0, 1:]) != targets:
                    continue
                # Diversify resources, health, delayed attacks and marks via legal play.
                for _ in range(int(rng.integers(0, 17))):
                    choices = []
                    for seat in range(2):
                        legal = [int(a) for a in np.flatnonzero(arena.masks[0, seat]) if a not in acquisitions]
                        choices.append(int(rng.choice(legal)))
                    prefix.append(choices)
                    arena.step(np.array([choices], np.int32))
                    if arena.results[0, 0]:
                        break
                if arena.results[0, 0] or list(arena.info[0, 1:]) != targets:
                    continue
                key = hashlib.sha256(arena.observations.tobytes()).hexdigest()
                if key in seen:
                    continue
                seen.add(key)
                entries.append({'id': len(entries), 'roles': targets, 'round': int(arena.info[0, 0]),
                                'prefix': prefix, 'observation_sha256': key})
                made += 1
    payload = {'seed': seed, 'roles': ROLES, 'per_ordered_pair': per_pair,
               'source': 'legal rule-generated prefixes, no learned policy or human data',
               'entries': entries, 'attempts': attempts, 'excluded_bank': exclude}
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(payload, separators=(',', ':')))
    print(json.dumps({'output': output, 'entries': len(entries), 'attempts': attempts,
                      'rounds': dict(collections.Counter(e['round'] for e in entries))}), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', default='curriculum-bank.json')
    parser.add_argument('--per-pair', type=int, default=64)
    parser.add_argument('--seed', type=int, default=2026100840)
    parser.add_argument('--exclude')
    args = parser.parse_args()
    build(args.output, args.per_pair, args.seed, args.exclude)
