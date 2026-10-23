"""Small shared helpers for reproducible training outputs."""
# 训练协议记录“用了什么配置/文件”，进度 JSON 则供外部监控读取。
import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


def settings(args: argparse.Namespace) -> dict[str, Any]:
    # func 是 argparse 保存的 Python 回调，不能写进 JSON 配置。
    return {key: value for key, value in vars(args).items() if key != "func"}


def digest(path: str | Path) -> str:
    # 对规则库、课程库或初始 checkpoint 做内容校验，便于追溯实验输入。
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def write_json(path: Path, value: Any) -> None:
    # 先写同目录临时文件再替换，避免监控读到只写了一半的 JSON。
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2), encoding="utf-8")
    temporary.replace(path)
