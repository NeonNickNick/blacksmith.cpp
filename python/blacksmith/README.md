# Blacksmith 原生绑定包

本包安装 CMake 构建的 C++ 扩展，向 Python 提供游戏规则、动作表、状态编码、
批量 `Arena` 和分支模拟 `SearchTree`。绑定源码位于相邻的 `../python-bindings/`；
训练与神经网络代码位于 `../src/master/`。

`setup.py` 中的 `BinaryDistribution.has_ext_modules()` 返回 `True`，让 wheel 带上
正确的平台标记。这不是 C++ 编译入口，也不负责训练或导出模型权重。
构建流程由仓库根目录的 `build.cmd` / `build.sh` 和 CMake 配置负责。

Python 文件的完整职责索引见 [Python README](../README.md)。
