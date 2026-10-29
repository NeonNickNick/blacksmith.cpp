"""LAN web front end for the Blacksmith Master local play engine."""
from __future__ import annotations

import argparse
import json
import mimetypes
import socket
import sys
import threading
import uuid
from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import urlparse

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python" / "src"))

from blacksmith import master as engine  # noqa: E402
from master import play  # noqa: E402
from master.agent import Agent  # noqa: E402

WEB_ROOT = Path(__file__).resolve().parent / "web"
RESOURCE_KEYS = ("cross", "dream", "spirit", "fire", "water", "wood", "earth", "bolt")


@dataclass
class Match:
    arena: Any
    agent: Agent
    record: play.GameRecord
    model_name: str
    chosen: int
    human_mask: np.ndarray
    phase: str = "playing"
    review_position: int = 0
    review_cache: dict[int, dict[str, Any]] = field(default_factory=dict)
    trial_arena: Any = None
    trial_chosen: int | None = None
    trial_mask: np.ndarray | None = None
    trial_finished: bool = False
    lock: threading.RLock = field(default_factory=threading.RLock)


class MatchService:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.pool = play.CheckpointPool(args.checkpoint, args.checkpoint_dir, args.seed)
        self.pool_lock = threading.Lock()
        self.sessions: dict[str, Match] = {}
        self.sessions_lock = threading.Lock()

    def new_match(self, iron: float, checkpoint_name: str | None = None) -> tuple[str, Match]:
        if iron not in (0.0, 0.5, 1.0):
            raise ValueError("Starting iron must be 0, 0.5, or 1.")
        if checkpoint_name:
            path = next((item for item in self.pool.paths if item.name == checkpoint_name), None)
            if path is None:
                raise ValueError("Selected model is not in the configured checkpoint pool.")
            with self.pool_lock:
                seed = self.pool.rng.getrandbits(32)
        else:
            with self.pool_lock:
                path, seed = self.pool.next()
        arena = play.GameRecord(iron).state_at(0, self.args.threads)
        agent = Agent(path, self.args.device, seed, self.args.width, self.args.leaf_width,
                      self.args.regret_iterations, self.args.search_depth,
                      self.args.search_nodes, self.args.search_batch)
        chosen = agent.choose(arena, 1)
        _, masks = arena.observe()
        match = Match(arena, agent, play.GameRecord(iron), path.name,
                      chosen, masks[0, 0].copy())
        session_id = uuid.uuid4().hex
        with self.sessions_lock:
            self.sessions[session_id] = match
        return session_id, match

    def get(self, session_id: str) -> Match:
        with self.sessions_lock:
            match = self.sessions.get(session_id)
        if match is None:
            raise KeyError("Match not found. Start a new game.")
        return match

    def lobby(self) -> dict[str, Any]:
        return {
            "models": [path.name for path in self.pool.paths],
            "starting_iron": self.args.starting_iron,
            "default_model": self.pool.paths[0].name if len(self.pool.paths) == 1 else None,
            "host": self.args.host,
            "port": self.args.port,
        }

    def state(self, match: Match, arena=None, *, trial=False) -> dict[str, Any]:
        arena = arena or match.arena
        status = arena.status(0)
        finished = bool(status["outcome"])
        current_round = int(status["round"]) - int(finished)
        players = []
        for seat, player in enumerate(status["players"]):
            players.append({
                "seat": seat,
                "label": "Player" if seat == 0 else "Master",
                "hp": float(player["hp"]), "mhp": float(player["mhp"]),
                "role": engine.ROLES[int(player["role"])],
                "iron": float(player["iron"]), "gold_iron": float(player["gold_iron"]),
                "magic": float(player["magic"]), "space": float(player["space"]),
                "time": float(player["time"]),
                "resources": {key: float(player[key]) for key in RESOURCE_KEYS},
            })
        _, masks = arena.observe()
        legal_mask = match.trial_mask if trial else match.human_mask
        # A review or resumed trial has its own live legal-action mask.
        if arena is not match.arena and not trial:
            legal_mask = masks[0, 0]
        legal_actions = [engine.ACTION_NAMES[i] for i, valid in enumerate(legal_mask) if valid]
        prediction = match.agent.predict_score(arena, 1) if not finished else None
        search_info = match.agent.last_search
        return {
            "round": current_round,
            "outcome": int(status["outcome"]),
            "finished": finished,
            "players": players,
            "model": match.model_name,
            "master_score": prediction,
            "search": None if search_info is None else {
                "depth": int(search_info.completed_depth),
                "requested_depth": int(search_info.requested_depth),
                "nodes": int(search_info.expanded_nodes),
                "budget_exhausted": bool(search_info.budget_exhausted),
            },
            "legal_actions": legal_actions,
            "phase": "trial" if trial else match.phase,
            "trial_action_complete": bool(trial and match.trial_finished),
        }

    def begin_round(self, match: Match, arena, trial=False) -> None:
        chosen = match.agent.choose(arena, 1)
        _, masks = arena.observe()
        if trial:
            match.trial_chosen = chosen
            match.trial_mask = masks[0, 0].copy()
        else:
            match.chosen = chosen
            match.human_mask = masks[0, 0].copy()

    def move(self, match: Match, text: str, trial=False) -> dict[str, Any]:
        arena = match.trial_arena if trial else match.arena
        mask = match.trial_mask if trial else match.human_mask
        chosen = match.trial_chosen if trial else match.chosen
        if arena is None or chosen is None or (trial and match.trial_finished):
            raise ValueError("There is no active round to play.")
        command = text.strip().lower()
        if command in ("q", "quit"):
            if trial:
                match.trial_arena = None
                return {"trial_left": True, "state": self.review_state(match)}
            match.phase = "finished"
            match.record.ending = "Player left the game."
            return {"state": self.state(match), "ending": match.record.ending}
        if command in ("resign", "surrender", "认输"):
            if trial:
                match.trial_finished = True
                return {"trial_ending": "Player resigns. Master wins.",
                        "state": self.state(match, arena, trial=True)}
            match.record.ending = "Player resigns. Master wins."
            match.phase = "finished"
            return {"state": self.state(match), "ending": match.record.ending}
        try:
            action = play.parse_action(text)
        except ValueError as exc:
            raise ValueError(str(exc)) from exc
        if not mask[action]:
            raise ValueError("Rejected: skill unavailable for your current resources or profession.")
        actions = [action, int(chosen)]
        result = int(arena.step(np.array([actions], np.int32), reset_finished=False)[0][0])
        if trial:
            if result:
                match.trial_finished = True
                ending = play.result_text(result, 0)
            else:
                self.begin_round(match, arena, trial=True)
                ending = None
            return {"state": self.state(match, arena, trial=True),
                    "player_action": engine.ACTION_NAMES[action],
                    "master_action": engine.ACTION_NAMES[chosen], "ending": ending}
        match.record.moves.append((action, int(chosen)))
        if result:
            match.phase = "finished"
            match.record.ending = play.result_text(result, 0)
            ending = match.record.ending
        else:
            self.begin_round(match, arena)
            ending = None
        return {"state": self.state(match), "player_action": engine.ACTION_NAMES[action],
                "master_action": engine.ACTION_NAMES[chosen], "ending": ending}

    def review_state(self, match: Match) -> dict[str, Any]:
        position = match.review_position
        arena = match.record.state_at(position, self.args.threads)
        payload = self.state(match, arena)
        payload.update({
            "phase": "review", "position": position,
            "move_count": len(match.record.moves), "ending": match.record.ending,
            "previous_move": None if position == 0 else {
                "player": engine.ACTION_NAMES[match.record.moves[position - 1][0]],
                "master": engine.ACTION_NAMES[match.record.moves[position - 1][1]],
            },
        })
        return payload

    def review(self, match: Match, position: int) -> dict[str, Any]:
        if match.phase != "finished":
            raise ValueError("Review is available after a game ends.")
        if not 0 <= position <= len(match.record.moves):
            raise ValueError("Review position is outside the recorded game.")
        match.review_position = position
        return self.review_state(match)

    def recommendations(self, match: Match) -> dict[str, Any]:
        position = match.review_position
        if position in match.review_cache:
            return match.review_cache[position]
        arena = match.record.state_at(position, self.args.threads)
        if arena.status(0)["outcome"]:
            raise ValueError("Terminal position: there is no next move to recommend.")
        policy, info = match.agent.analyze(arena)
        recommendations = []
        for seat, label in ((0, "Player"), (1, "Master")):
            candidates = {int(i) for i in info.root_candidates[0, seat] if i >= 0}
            ranked = sorted(candidates, key=lambda i: (-float(policy[seat, i]), i))[:3]
            recommendations.append({"label": label, "actions": [
                {"name": engine.ACTION_NAMES[i], "weight": float(policy[seat, i])}
                for i in ranked
            ]})
        result = {"depth": int(info.completed_depth), "requested_depth": int(info.requested_depth),
                  "expanded_nodes": int(info.expanded_nodes),
                  "recommendations": recommendations}
        match.review_cache[position] = result
        return result

    def start_trial(self, match: Match) -> dict[str, Any]:
        if match.phase != "finished":
            raise ValueError("Trial is available from a finished game's review.")
        match.trial_arena = match.record.state_at(match.review_position, self.args.threads)
        if match.trial_arena.status(0)["outcome"]:
            raise ValueError("Cannot start a trial from a terminal position.")
        match.trial_finished = False
        self.begin_round(match, match.trial_arena, trial=True)
        return self.state(match, match.trial_arena, trial=True)


def make_handler(service: MatchService):
    class Handler(BaseHTTPRequestHandler):
        server_version = "BlacksmithServer/1.0"

        def log_message(self, fmt, *args):
            print(f"[{self.log_date_time_string()}] {self.address_string()} {fmt % args}")

        def send_json(self, payload, status=200):
            data = json.dumps(payload, ensure_ascii=False, allow_nan=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            origin = self.headers.get("Origin")
            if origin and origin == service.args.cors_origin:
                self.send_header("Access-Control-Allow-Origin", origin)
                self.send_header("Vary", "Origin")
            self.end_headers()
            self.wfile.write(data)

        def send_error_json(self, message, status=400):
            self.send_json({"error": message}, status)

        def read_json(self):
            length = int(self.headers.get("Content-Length", "0"))
            if length > 16_384:
                raise ValueError("Request body is too large.")
            if length == 0:
                return {}
            try:
                return json.loads(self.rfile.read(length).decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise ValueError("Request body must be valid JSON.") from exc

        def do_GET(self):
            parsed = urlparse(self.path)
            if parsed.path == "/api/config":
                self.send_json(service.lobby())
                return
            if parsed.path.startswith("/api/"):
                self.send_error_json("Unknown endpoint.", 404)
                return
            relative = "index.html" if parsed.path in ("", "/") else parsed.path.lstrip("/")
            target = (WEB_ROOT / relative).resolve()
            if WEB_ROOT.resolve() not in target.parents and target != WEB_ROOT.resolve():
                self.send_error(404)
                return
            if not target.is_file():
                self.send_error(404)
                return
            data = target.read_bytes()
            content_type = mimetypes.guess_type(target.name)[0] or "application/octet-stream"
            if content_type.startswith("text/") or content_type in ("application/javascript",):
                content_type += "; charset=utf-8"
            self.send_response(200)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            self.wfile.write(data)

        def do_OPTIONS(self):
            origin = self.headers.get("Origin")
            if origin != service.args.cors_origin:
                self.send_response(403)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            self.send_response(204)
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
            self.send_header("Access-Control-Allow-Headers", "Content-Type, ngrok-skip-browser-warning")
            self.send_header("Access-Control-Max-Age", "600")
            self.send_header("Vary", "Origin")
            self.send_header("Content-Length", "0")
            self.end_headers()

        def do_POST(self):
            try:
                payload = self.read_json()
                path = urlparse(self.path).path.rstrip("/")
                if path == "/api/new":
                    iron = float(payload.get("iron", 0))
                    checkpoint_name = payload.get("checkpoint")
                    session_id, match = service.new_match(iron, checkpoint_name)
                    self.send_json({"session": session_id, "state": service.state(match),
                                    "mode": {"iron": iron}})
                    return
                parts = path.split("/")
                if len(parts) < 4 or parts[1] != "api" or parts[2] != "match":
                    self.send_error_json("Unknown endpoint.", 404)
                    return
                match = service.get(parts[3])
                with match.lock:
                    if parts[4:] == ["move"]:
                        result = service.move(match, str(payload.get("action", "")))
                    elif parts[4:] == ["review"]:
                        result = {"state": service.review(match, int(payload.get("position", 0)))}
                    elif parts[4:] == ["recommendations"]:
                        result = service.recommendations(match)
                    elif parts[4:] == ["trial"]:
                        result = {"state": service.start_trial(match)}
                    elif parts[4:] == ["trial", "move"]:
                        result = service.move(match, str(payload.get("action", "")), trial=True)
                    elif parts[4:] == ["trial", "leave"]:
                        match.trial_arena = None
                        match.trial_finished = False
                        result = {"state": service.review_state(match)}
                    elif parts[4:] == ["close"]:
                        with service.sessions_lock:
                            service.sessions.pop(parts[3], None)
                        result = {"closed": True}
                    else:
                        self.send_error_json("Unknown endpoint.", 404)
                        return
                self.send_json(result)
            except KeyError as exc:
                self.send_error_json(str(exc).strip("'\""), 404)
            except (ValueError, TypeError, IndexError) as exc:
                self.send_error_json(str(exc))
            except Exception as exc:
                print(f"Request failed: {type(exc).__name__}: {exc}")
                self.send_error_json("The server could not process this action.", 500)

    return Handler


def configure(parser: argparse.ArgumentParser) -> None:
    checkpoints = parser.add_mutually_exclusive_group()
    checkpoints.add_argument("--checkpoint", help="Use one fixed model (web default: master-0099.pt)")
    checkpoints.add_argument("--checkpoint-dir", help="Randomly choose a .pt file each game")
    parser.add_argument("--host", default="0.0.0.0", help="Interface to listen on (default: all interfaces)")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--cors-origin", default="https://neonnicknick.github.io",
                        help="Allowed browser origin for API calls (default: GitHub Pages site)")
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--seed", type=int, default=None)
    parser.add_argument("--starting-iron", type=float, choices=(0.0, 0.5, 1.0))
    parser.add_argument("--search-depth", "--depth", type=int, default=2)
    parser.add_argument("--search-nodes", type=int, default=200000)
    parser.add_argument("--search-batch", type=int, default=4096)
    for name, default in (("width", 24), ("leaf-width", 12),
                          ("regret-iterations", 128), ("threads", 6), ("torch-threads", 4)):
        parser.add_argument("--" + name, type=int, default=default)


def local_addresses() -> list[str]:
    addresses = set()
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        # A UDP connect selects the route and local interface without sending a packet.
        probe.connect(("192.0.2.1", 80))
        address = probe.getsockname()[0]
        if not address.startswith("127."):
            addresses.add(address)
    except OSError:
        pass
    finally:
        probe.close()
    try:
        for result in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            address = result[4][0]
            if not address.startswith("127."):
                addresses.add(address)
    except OSError:
        pass
    return sorted(addresses)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    configure(parser)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if (min(args.threads, args.torch_threads, args.width, args.leaf_width,
            args.search_depth, args.search_nodes, args.search_batch, args.regret_iterations) <= 0
            or max(args.width, args.leaf_width) > engine.SKILL_NUM
            or args.search_depth > engine.ROUND_LIMIT):
        parser.error(f"Positive play limits required; widths <= {engine.SKILL_NUM}, depth <= {engine.ROUND_LIMIT}")
    torch.set_num_threads(args.torch_threads)
    service = MatchService(args)
    server = ThreadingHTTPServer((args.host, args.port), make_handler(service))
    print(f"Blacksmith Master server listening on {args.host}:{args.port}")
    print(f"This computer: http://127.0.0.1:{args.port}/")
    if args.host == "0.0.0.0":
        for address in local_addresses():
            print(f"Other devices: http://{address}:{args.port}/")
    elif args.host not in ("127.0.0.1", "localhost", "::1"):
        print(f"Other devices: http://{args.host}:{args.port}/")
    print("Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server…")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
