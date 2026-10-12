"""Small shared helpers for reproducible training outputs."""
import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def settings(args: argparse.Namespace) -> dict[str, Any]:
    return {key: value for key, value in vars(args).items() if key != "func"}


def digest(path: str | Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path: Path, value: Any) -> None:
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2), encoding="utf-8")
    temporary.replace(path)
