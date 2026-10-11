# Master

Master 是独立的神经网络机器人。C++20 负责游戏规则、合法动作、状态编码、批量环境与分支模拟；Python 负责网络、候选采样、矩阵搜索、regret matching、课程构造、replay 和训练。跨语言接口只传递批量数组和动作前缀。

## 数据协议

`SCHEMA_VERSION = 3`。每个玩家视角使用 1038 维 `float32` observation：

| 区域 | 长度 | 内容 |
| --- | ---: | --- |
| 己方公共状态 | 200 | 生命、普通铁/空间/时间、职业、授权技能、攻击、当前及待结算防御 |
| 对方公共状态 | 200 | 同上 |
| 己方职业状态 | 238 | 魔力、金铁、炼金/副职业、标记、待结算资源、回调、效果 |
| 对方职业状态 | 238 | 同上 |
| 全局状态 | 2 | 当前回合、剩余回合 |
| 历史 | 8 × 20 | 最近八个已完成回合，包含双方动作及结算信息 |

公共状态最前面的三个字段是 `HP/10`、`MHP/10`、`(MHP-HP)/10`。资源和攻击/防御强度通常除以 10，有限持续时间除以 5。无限持续单独编码，不与有限持续时间混合。

攻击、防御、标记、资源、回调和效果按类型编码；排队数据按延迟 `0`、`1`、`2及以上` 分组。攻击保留强度、穿甲加权强度、有限持续时间及无限数量。防御保留强度和持续时间。待结算防御的延迟使用调度时钟，持续时间使用将要生效的防御实体时钟。标记、回调和效果保留数量及持续时间。HP/MHP 的待结算资源独立编码。

历史从旧到新排列，不足八回合时在左侧补零。每个历史 token 为：

```text
有效标记, 己方动作ID, 对方动作ID, 已完成回合/100,
己方[结算后HP/10, ΔHP, ΔMHP, Δ普通铁, Δ金铁, Δ魔力, Δ空间, Δ时间],
对方[同上]
```

所有差值均除以 10。有效标记区分动作 ID 0 和填充。双方视角交换时，公共/职业数据和历史中的双方动作、数值同步交换。历史只在双方动作结算后写入；搜索分支维护自己的历史，不修改真实环境。

输出包含 75 个动作，参数范围保持 0～5；魔法攻击/护盾及空间攻击/屏障仅保留正参数。动作顺序由独立 C++ 编译期动作表定义，`ACTIONS`、`ACTION_NAMES`、各区域偏移和维度均由绑定导出。

## 网络与训练

双方公共状态共享一个字段编码器，双方职业状态共享另一个编码器。历史通过动作 embedding、结算数值投影和位置 embedding 输入 Transformer，使用始终有效的摘要 token 聚合，屏蔽填充回合。两个分支融合后进入共享 MLP，输出策略 logits 和 `[-1, 1]` 状态价值。

默认 MLP 隐藏宽度 256、深度 3，历史 Transformer 宽度 64、2 层、4 个注意力头，无 dropout。`--history-layers 0` 可关闭历史分支；`--hidden`、`--depth`、`--history-width`、`--history-heads` 可调整架构。是否改善胜率需通过实际训练及均衡席位对战评估。

强化学习仍采用两层同时行动搜索、候选动作采样、收益矩阵和 regret matching+。折扣为 0.997；策略目标来自搜索，价值目标来自终局结果。课程前缀仅用于构造起始状态和行为历史，不作为训练目标。

职业编号与引擎枚举一致：`none=0, warlock=1, cannon=2, driver=3, lancer=4, bloodsigil=5`。Alchemy 是 warlock 的技能；使用它解锁 MIDASTOUCH，但职业仍为 warlock。血印入口授予独立的 bloodsigil 技能组。

`free` 模式从初始状态自我对弈。`mixed` 每十个起始对局包含四个初始状态和六个合法构造状态。课程覆盖五种职业的全部 25 个有序组合，包括五种同职业对战；每轮 25 个课程对局恰好使用各组合一次，轮内打乱顺序。Replay 按初始对局和 25 个职业组合分层，训练权重分别为 40% 和每组合 2.4%，避免对局长度改变预期比例。日志中的课程 group 为组合索引，映射记录在 `protocol.json` 的 `course_pairs` 字段。

课程默认包含 bloodsigil，无需额外开关。Mixed batch 至少 42 条，replay 容量至少 2667，确保每个分层有采样名额和至少 64 条容量。训练达到 `--warmup` 且所有分层样本就绪后才开始更新；未就绪时继续收集，并在进度中记录 `replay_ready`。职业协议升级后需重新生成课程库；旧 schema 的 checkpoint 会被拒绝。

checkpoint 保存 CPU 上的模型权重、完整架构配置、schema 及训练进度。加载采用明确路径并检查兼容性。`--resume` 恢复模型、优化器及计数，重新构建 replay 和对局；`--initial` 只加载模型。改变设备不要求修改模型代码。

## 构建

需要 Python 3.11、C++20 编译器、CMake 3.21+ 和 uv。先同步绑定环境，再构建扩展，最后同步训练环境，确保安装的是本机编译的扩展。

Windows 可以在仓库根目录运行 `build.cmd`，或在 MSVC 开发者终端执行：

```powershell
uv sync --project python/blacksmith
cmake -S . -B out/build/master -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build out/build/master --target core master
uv sync --project python --reinstall-package blacksmith
```

Linux / 远程 GPU 主机可在仓库根目录运行脚本：

```bash
bash build.sh
# 可选配置参数传给 CMake；编译器默认由 CMake 选择，也可使用 CXX 环境变量。
bash build.sh -DBLACKSMITH_USE_OPENMP=OFF
```

`build.sh` 与 `build.cmd` 的步骤一致：同步绑定环境、配置 Release、构建全部 C++ 目标、生成两个模块的类型存根、构建 wheel、同步训练环境、重新安装新 wheel。Linux 构建目录为 `out/build/linux-release`，无需 Ninja。脚本也可通过绝对路径从任意目录调用，在任一步失败时停止，使用本次生成的 wheel，支持含空格路径；`CMAKE_BUILD_PARALLEL_LEVEL` 可控制构建并发。

也可以手动执行：

```bash
uv sync --project python/blacksmith
cmake -S . -B out/build/master -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/python/blacksmith/.venv/bin/python"
cmake --build out/build/master --target core master --parallel
uv sync --project python --reinstall-package blacksmith
```

CMake 支持显式指定 `Python3_EXECUTABLE`，否则优先使用本项目绑定环境，再寻找系统解释器。OpenMP 可选，缺失时使用串行实现；`-DBLACKSMITH_USE_OPENMP=OFF` 可显式关闭。扩展 wheel 使用当前 Python/平台标签，应在目标机器重新编译，不复制 Windows 的二进制到 Linux。

各平台统一要求标准 C++20，关闭编译器扩展；单配置生成器未指定配置时默认 Release。CMake 会检查 ranges、span 和浮点模板参数支持，避免旧编译器进入正式构建后失败。Linux 同样需要 Python 3.11 的开发头文件及 pybind11。

GPU 主机需安装与驱动兼容的 CUDA 版 PyTorch，确认下面命令输出 `True` 后使用 `--device cuda`：

```bash
uv run --project python python -c "import torch; print(torch.cuda.is_available())"
```

## 验证与运行

下列命令均在仓库根目录运行。CPU 验证只检查逻辑和训练链路，不代表模型已具备对战能力。
使用 `build.cmd` 时，将 CTest 构建目录改为 `out/build/x64-release`；使用手动构建示例时改为 `out/build/master`。

```bash
ctest --test-dir out/build/linux-release -R test-master-schema --output-on-failure
uv run --project python python -m unittest discover -s python/tests -v
uv run --project python master --help
uv run --project python master train --mode free --device cpu \
  --envs 4 --threads 1 --width 2 --leaf-width 2 --regret-iterations 8 \
  --hidden 32 --history-width 16 --history-heads 2 --history-layers 1 \
  --horizon 100 --updates 1 --warmup 1 --batches 1 --batch-size 32 \
  --replay 2048 --output out/master-cpu
```

PowerShell 的多行命令请合并为一行，或使用其续行语法。输出目录必须不存在，防止覆盖已有实验。

```bash
uv run --project python master curriculum --per-pair 64 --output out/master-curriculum.json
uv run --project python master train --mode mixed --bank out/master-curriculum.json \
  --device cuda --output out/master-gpu
uv run --project python master train --mode mixed --bank out/master-curriculum.json \
  --device cuda --resume out/master-gpu/master-1000.pt --output out/master-gpu-resumed
uv run --project python master play --checkpoint out/master-gpu/master-1000.pt
uv run --project python master evaluate --first out/master-gpu/master-1000.pt \
  --second out/master-gpu-resumed/master-2000.pt --games 200 --output out/master-evaluation.json
```

每次训练记录规则二进制及课程库摘要、模型配置、起始权重摘要、逐步动作和终局统计，方便在 CPU 与 GPU 环境核对实验协议。Python 测试覆盖历史视角交换、截断、分支复制、填充屏蔽、梯度和 checkpoint，以及合法搜索、串行/并行一致性、课程和 replay。检测到 CUDA 时还会执行 GPU 搜索、反向传播及 GPU checkpoint 在 CPU 上加载的测试；无 CUDA 时自动跳过这一项。
