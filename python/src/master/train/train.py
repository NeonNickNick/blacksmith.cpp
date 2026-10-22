"""Self-play with search-policy targets and terminal-value targets."""
import argparse
from collections import Counter
from dataclasses import asdict
import os
import hashlib
import json
from pathlib import Path
import signal
import time
from typing import Any

import numpy as np
import torch
from torch.nn import functional as F
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
    if (args.max_seconds < 0 or args.max_gradient_steps < 0 or args.lr_time_offset < 0
            or not 0 <= args.min_lr <= args.lr):
        raise ValueError("Invalid training budget or minimum learning rate")
    torch.set_num_threads(args.torch_threads)
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device(args.device)
    if args.precision == "bf16" and (device.type != "cuda" or not torch.cuda.is_bf16_supported()):
        raise ValueError("BF16 training requires a compatible CUDA device")
    torch.set_float32_matmul_precision("high")
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
        # Keep momentum but honor the requested learning rate on continuation.
        for group in optimizer.param_groups:
            group["lr"] = args.lr
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
    torch.save(checkpoint(model, stage="selfplay", update=start_update, games=games,
                         gradient_steps=gradients, optimizer=optimizer.state_dict(),
                         config=settings(args)), output / f"master-{start_update:04d}.pt")
    interrupted = []
    def request_stop(signum, frame):
        interrupted.append(signum)
    for signum in (signal.SIGTERM, signal.SIGINT):
        signal.signal(signum, request_stop)
    def stop_reason():
        if interrupted:
            return "signal"
        if args.max_seconds and time.monotonic() - start >= args.max_seconds:
            return "time_budget"
        if args.max_gradient_steps and gradients >= args.max_gradient_steps:
            return "gradient_budget"
        return None
    collected = 0
    def trace_path(name):
        return output / name if args.record_games else Path(os.devnull)

    with trace_path("selfplay-moves.csv").open("w", encoding="utf-8") as moves, \
            trace_path("starts.jsonl").open("w", encoding="utf-8") as start_log, \
            trace_path("terminals.jsonl").open("w", encoding="utf-8") as terminal_log:
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
                if stop_reason():
                    break
                observations, masks = arena.observe()
                roles_before = arena.metadata()[:, 1:]
                with torch.autocast(device_type=device.type, dtype=torch.bfloat16,
                                    enabled=args.precision == "bf16"):
                    choices, policies = search(model, arena, rng, device, args.width,
                                               args.leaf_width, args.regret_iterations, .15,
                                               (observations, masks))
                collected += 2 * args.envs
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
            learner_start = time.monotonic()
            losses: list[list[float]] = []
            ready = replay.ready(args.batch_size)
            if local_update >= args.warmup and ready:
                for _ in range(args.batches):
                    if stop_reason():
                        break
                    fraction = min(1., gradients / args.max_gradient_steps) if args.max_gradient_steps else 0.
                    if args.max_seconds:
                        fraction = max(fraction, min(1., (time.monotonic() - start + args.lr_time_offset)
                                                      / (args.max_seconds + args.lr_time_offset)))
                    lr = float(args.min_lr + .5 * (args.lr - args.min_lr) * (1 + np.cos(np.pi * fraction)))
                    for group in optimizer.param_groups:
                        group["lr"] = lr
                    batch = replay.sample(rng, args.batch_size)
                    x, masks_t, policy_t, value_t, weights = (torch.as_tensor(array, device=device) for array in batch)
                    with torch.autocast(device_type=device.type, dtype=torch.bfloat16,
                                        enabled=args.precision == "bf16"):
                        logits, values = model(x, value_logits=True)
                        pl = (-(policy_t * masked_log_probs(logits, masks_t)).sum(-1) * weights).sum()
                        target = (value_t + 1) * .5
                        vl = (F.binary_cross_entropy_with_logits(values.float(), target,
                                                                 reduction="none") * weights).sum()
                    loss = pl + vl
                    if not torch.isfinite(loss):
                        raise RuntimeError("Nonfinite self-play loss")
                    optimizer.zero_grad(set_to_none=True)
                    loss.backward()
                    torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0, error_if_nonfinite=True)
                    optimizer.step()
                    gradients += 1
                    losses.append([pl.item(), vl.item()])
            status = {"update": update, "games": games, "gradient_steps": gradients,
                      "elapsed_seconds": time.monotonic() - start,
                      "search_seconds": search_seconds,
                      "learner_seconds": time.monotonic() - learner_start,
                      "collected_player_positions": collected,
                      "replay_draws": (gradients - (saved["gradient_steps"] if args.resume else 0)) * args.batch_size,
                      "learning_rate": optimizer.param_groups[0]["lr"],
                      "loss": np.mean(losses, axis=0).tolist() if losses else [],
                      "replay_ready": ready,
                      "pool_sizes": replay.sizes(), "started": dict(started),
                      "finished": dict(finished), "observed_roles": dict(observed_roles),
                      "terminal_roles": dict(terminal_roles)}
            print(json.dumps(status), flush=True)
            write_json(output / "progress.json", status)
            ending = stop_reason()
            if update % args.save_every == 0 or local_update == args.updates or ending:
                destination = output / f"master-{update:04d}.pt"
                temporary = destination.with_suffix(".tmp")
                torch.save(checkpoint(model, stage="selfplay", update=update, games=games,
                           gradient_steps=gradients, optimizer=optimizer.state_dict(),
                           config=settings(args)), temporary)
                temporary.replace(destination)
            for handle in (moves, start_log, terminal_log):
                handle.flush()
            if ending:
                break
        status.update({"checkpoint": destination.name, "complete": True,
                       "stop_reason": ending or "update_budget",
                       "unfinished_games_excluded_from_replay": sum(bool(history) for history in histories)})
        write_json(output / "completion.json", status)


def configure(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--mode", choices=("free", "mixed"), default="mixed")
    parser.add_argument("--bank")
    parser.add_argument("--record-games", action="store_true", help="Save optional per-move and episode trace logs")
    parser.add_argument("--output", required=True)
    parser.add_argument("--initial")
    parser.add_argument("--resume")
    parser.add_argument("--device", default="cuda" if torch.cuda.is_available() else "cpu")
    parser.add_argument("--precision", choices=("fp32", "bf16"), default="fp32")
    parser.add_argument("--max-seconds", type=float, default=0)
    parser.add_argument("--lr-time-offset", type=float, default=0,
                        help="Elapsed schedule seconds before resume; does not extend the runtime budget")
    parser.add_argument("--max-gradient-steps", type=int, default=0)
    parser.add_argument("--min-lr", type=float, default=0)
    for name, default in (("envs", 32), ("threads", 12), ("width", 8), ("leaf-width", 4),
                          ("horizon", 32), ("updates", 1000), ("batches", 24),
                          ("batch-size", 512), ("replay", 131072), ("regret-iterations", 64),
                          ("seed", 42), ("save-every", 100), ("warmup", 10),
                          ("hidden", 256), ("depth", 4), ("history-width", 64),
                          ("history-layers", 1), ("history-heads", 4), ("torch-threads", 1)):
        parser.add_argument("--" + name, type=int, default=default)
    parser.add_argument("--lr", type=float, default=.0003)
    parser.set_defaults(func=run)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
