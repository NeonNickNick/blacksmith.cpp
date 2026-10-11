# Blacksmith Python

`src/master` 实现包含状态 MLP 和行为历史 Transformer 的神经网络机器人，使用搜索引导的强化学习。C++ 提供规则、编码和批量模拟，训练逻辑位于 Python。

详细协议、Windows/Linux 构建、CPU 验证与远程 GPU 训练命令见 [Master 文档](src/master/README.md)。

从仓库根目录运行 `build.cmd` 构建 Windows 扩展，再执行 `uv run --project python master --help`。
Linux 使用 `bash build.sh`，默认采用系统编译器；强化训练默认采用 40% 初始对局和 60% 五职业均衡课程对局。
