"""Self-play with search-policy targets and terminal-value targets."""
import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import time
from typing import Any

import numpy as np
import torch
from blacksmith import master as engine

from master.models import ModelConfig, Policy, checkpoint, load, masked_log_probs
from master.models.data import History
from master.runtime import digest, settings, write_json
from .replay import Starts, StratifiedReplay
from .search import search
from .sampling import COURSE_PAIRS, MIN_MIXED_BATCH, MIN_MIXED_REPLAY, masses


def run(args: argparse.Namespace) -> None:
    integers = (args.envs, args.threads, args.width, args.leaf_width, args.horizon,
                args.updates, args.batches, args.batch_size, args.replay,
                args.regret_iterations, args.save_every, args.warmup, args.hidden,
                args.torch_threads)
    if min(integers) <= 0 or max(args.width, args.leaf_width) > engine.SKILL_NUM or args.lr <= 0:
        raise ValueError("Invalid self-play configuration")
    if args.mode == "mixed" and (args.batch_size < MIN_MIXED_BATCH or args.replay < MIN_MIXED_REPLAY):
        raise ValueError(f"Mixed replay needs batch_size >= {MIN_MIXED_BATCH} and capacity >= {MIN_MIXED_REPLAY}")
    if args.resume and args.initial:
        raise ValueError("Use either --initial or --resume")
    torch.set_num_threads(args.torch_threads)
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device(args.device)
    source = args.resume or args.initial
    config = ModelConfig(hidden=args.hidden, depth=args.depth, history_width=args.history_width,
                         history_layers=args.history_layers, history_heads=args.history_heads)
    model = (load(source) if source else Policy(config=config)).to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    start_update = games = gradients = 0
    if args.resume:
        saved: dict[str, Any] = torch.load(args.resume, map_location="cpu", weights_only=True)
        if saved.get("stage") != "selfplay":
            raise ValueError("--resume requires a self-play checkpoint; use --initial for a model-only checkpoint")
        optimizer.load_state_dict(saved["optimizer"])
        start_update, games, gradients = saved["update"], saved["games"], saved["gradient_steps"]
    arena = engine.Arena(args.envs, args.threads)
    replay = StratifiedReplay(args.mode, args.replay)
    starts = Starts(args.mode, args.bank, args.seed + 1)
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    initial_hash = hashlib.sha256(b"".join(value.detach().cpu().numpy().tobytes()
                                           for value in model.state_dict().values())).hexdigest()
    write_json(output / "protocol.json", {
        "config": settings(args), "initial_weights_sha256": initial_hash,
        "initial_checkpoint_sha256": digest(source) if source else None,
        "rules_sha256": digest(engine.__file__), "schema_version": engine.SCHEMA_VERSION,
        "model_config": asdict(model.config), "device": str(device),
        "replay_masses": masses(args.mode), "course_pairs": COURSE_PAIRS,
        "bank_sha256": digest(args.bank) if args.mode == "mixed" else None,
        "empty_replay": True, "prefixes_used_as_targets": False,
        "resume_scope": "model and optimizer; fresh replay and episodes"})
    histories: list[History] = [[] for _ in range(args.envs)]
    groups = [-1] * args.envs
    generations = [0] * args.envs
    started: Counter[str] = Counter()
    finished: Counter[str] = Counter()
    observed_roles: Counter[str] = Counter()
    terminal_roles: Counter[str] = Counter()
    start = time.monotonic()
    with (output / "selfplay-moves.csv").open("w", encoding="utf-8") as moves, \
            (output / "starts.jsonl").open("w", encoding="utf-8") as start_log, \
            (output / "terminals.jsonl").open("w", encoding="utf-8") as terminal_log:
        moves.write("update,env,game,round,first_action,second_action,outcome,first_hp,second_hp\n")

        def reset(env: int) -> None:
            group, prefix = starts.next()
            groups[env] = group
            arena.reset(env, prefix)
            started[str(group)] += 1
            start_log.write(json.dumps({"env": env, "game": generations[env],
                                        "group": group, "prefix": prefix}) + "\n")

        for env in range(args.envs):
            reset(env)
        for local_update in range(1, args.updates + 1):
            update = start_update + local_update
            model.eval()
            search_start = time.monotonic()
            for _ in range(args.horizon):
                observations, masks = arena.observe()
                roles_before = arena.metadata()[:, 1:]
                choices, policies = search(model, arena, rng, device, args.width,
                                           args.leaf_width, args.regret_iterations, .15,
                                           (observations, masks))
                for env in range(args.envs):
                    histories[env].append((observations[env].copy(), masks[env].copy(), policies[env].copy()))
                    observed_roles.update(str(int(role)) for role in roles_before[env])
                results = arena.step(choices)
                for env, result in enumerate(results):
                    outcome = int(result[0])
                    moves.write(f"{update},{env},{generations[env]},{int(result[1])},"
                                f"{choices[env, 0]},{choices[env, 1]},{outcome},{result[2]},{result[3]}\n")
                    if not outcome:
                        continue
                    replay.add(groups[env], histories[env], outcome)
                    histories[env].clear()
                    roles = [int(result[4]), int(result[5])]
                    terminal_roles.update(str(role) for role in roles)
                    terminal_log.write(json.dumps({"update": update, "env": env,
                        "game": generations[env], "group": groups[env], "roles": roles,
                        "outcome": outcome, "rounds": int(result[1])}) + "\n")
                    finished[str(groups[env])] += 1
                    games += 1
                    generations[env] += 1
                    reset(env)
            search_seconds = time.monotonic() - search_start
            model.train()
            losses: list[list[float]] = []
            ready = replay.ready(args.batch_size)
            if local_update >= args.warmup and ready:
                for _ in range(args.batches):
                    batch = replay.sample(rng, args.batch_size)
                    x, masks_t, policy_t, value_t, weights = (torch.as_tensor(array, device=device) for array in batch)
                    logits, values = model(x)
                    pl = (-(policy_t * masked_log_probs(logits, masks_t)).sum(-1) * weights).sum()
                    vl = ((values - value_t).square() * weights).sum()
                    loss = pl + vl
                    if not torch.isfinite(loss):
                        raise RuntimeError("Nonfinite self-play loss")
                    optimizer.zero_grad(set_to_none=True)
                    loss.backward()
                    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
                    optimizer.step()
                    gradients += 1
                    losses.append([pl.item(), vl.item()])
            status = {"update": update, "games": games, "gradient_steps": gradients,
                      "elapsed_seconds": time.monotonic() - start,
                      "search_seconds": search_seconds,
                      "loss": np.mean(losses, axis=0).tolist() if losses else [],
                      "replay_ready": ready,
                      "pool_sizes": replay.sizes(), "started": dict(started),
                      "finished": dict(finished), "observed_roles": dict(observed_roles),
                      "terminal_roles": dict(terminal_roles)}
            print(json.dumps(status), flush=True)
            write_json(output / "progress.json", status)
            if update % args.save_every == 0 or local_update == args.updates:
                destination = output / f"master-{update:04d}.pt"
                torch.save(checkpoint(model, stage="selfplay", update=update, games=games,
                           gradient_steps=gradients, optimizer=optimizer.state_dict(),
                           config=settings(args)), destination)
                for handle in (moves, start_log, terminal_log):
                    handle.flush()
        status.update({"checkpoint": destination.name, "complete": True,
                       "unfinished_games_excluded_from_replay": sum(bool(history) for history in histories)})
        write_json(output / "completion.json", status)


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--mode", choices=("free", "mixed"), default="mixed")
    parser.add_argument("--bank")
    parser.add_argument("--output", required=True)
    parser.add_argument("--initial")
    parser.add_argument("--resume")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    for name, default in (("envs", 32), ("threads", 12), ("width", 8), ("leaf-width", 4),
                          ("horizon", 32), ("updates", 1000), ("batches", 24),
                          ("batch-size", 512), ("replay", 131072), ("regret-iterations", 64),
                          ("seed", 42), ("save-every", 100), ("warmup", 10),
                          ("hidden", 256), ("depth", 3), ("history-width", 64),
                          ("history-layers", 2), ("history-heads", 4), ("torch-threads", 1)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.add_argument("--lr", type=float, default=.0003)
    parser.set_defaults(func=run)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
