"""Blacksmith neural master: search-guided self-play and local inference."""
# 包入口负责命令分发；规则模拟、模型和训练循环各自由对应模块实现。
import argparse


def main() -> None:
    # 在真正运行命令时导入子模块，普通 import master 不立即导入 PyTorch/原生扩展。
    from .train import curriculum, train
    from . import play

    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    for name, module in (("curriculum", curriculum), ("train", train), ("play", play)):
        # configure() 同时注册参数和 args.func，解析后无需再写一套分支调度。
        module.configure(commands.add_parser(name, description=module.__doc__))
    args = parser.parse_args()
    args.func(args)
