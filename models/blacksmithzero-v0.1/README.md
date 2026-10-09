# BlacksmithZero v0.1

打铁 AI：策略与价值网络、同时行动搜索、命令行对战，以及从零自我对弈训练工具。

网络是三层 256 宽的 MLP，使用 Tanh，输入 718 维公开局面，输出 156 个动作评分和一个局面价值，共 355,997 个参数。实际出招由网络和两层搜索共同决定。C++ 负责游戏规则与局面展开，Python/PyTorch 负责搜索调度、网络推理和训练。

游戏规则直接编译自本仓库的 `src/blacksmith-core`。所有 AI 文件集中在本目录，可以与 `models` 中的其他版本并列保存。

## 运行对战

在本目录打开终端。需要 Python 3.10 或更高版本、CMake 3.21 或更高版本，以及支持本项目 C++20 模板的编译器；Windows 可使用 w64devkit 的 GCC 15。

```sh
python -m pip install -r requirements.txt
python build.py
python play.py
```

Windows 也可以双击 `play.cmd`。它会先增量构建当前仓库规则，再进入持续运行的对战窗口。

每回合显示血量、资源、延迟攻击、防御和合法技能。输入菜单编号或英文技能名；需要参数时，例如 `shield 2`。`new` 重开，`q` 退出。AI 在等待输入前锁定动作，提交后双方同时揭晓，每盘交换位置。

`config.json` 控制陪玩和评测设置：

| 设置 | 默认值 | 用途 |
|---|---:|---|
| `checkpoint` | `checkpoint.pt` | 权重路径，相对本目录 |
| `device` | `cpu` | 网络推理设备，也可设为 `cuda` |
| `root_width` | 24 | 第一层每方最多保留的动作数 |
| `leaf_width` | 12 | 第二层每方最多保留的动作数 |
| `regret_iterations` | 128 | 每个同时行动矩阵的求解次数 |
| `torch_threads` | 4 | PyTorch CPU 线程数 |
| `engine_threads` | 6 | C++ 局面展开线程数 |

## 训练方法

### 随机初始化与自我对弈

`train_zero.py` 随机初始化一个新网络，建立空经验池，然后与自身对弈。它不加载随附的 `checkpoint.pt` 来开始训练。

模型学习两个任务：策略头学习搜索得到的动作分布，价值头学习对局最终的胜负。两者共用三层 MLP。职业、攻击、资源转换和防御的效果由游戏引擎计算。

每一步的数据生成过程如下：

1. 引擎给出双方的公开局面及合法动作。
2. 网络计算动作先验。若合法动作数超过搜索宽度，按先验进行无放回采样；否则枚举全部合法动作。
3. 第一层展开双方候选动作的所有组合；每个后继局面再展开一层双方动作组合。
4. 已结束的叶节点使用胜负值 `+1 / -1 / 0`；其他叶节点由网络估值，并结合双方视角形成零和收益。
5. 每层的同时行动矩阵通过 regret matching+ 求解，得到双方各自的混合策略，再分别采样出招。
6. 保存实际经过的局面、合法动作掩码和搜索策略。整盘结束后补上胜负标签，加入经验池。

这是 AlphaZero 类的“自我对弈 → 搜索改进策略 → 网络学习 → 再搜索”循环。打铁双方同时出招，因此采用联合动作矩阵和 regret matching+，没有采用轮流行动的 PUCT 树搜索。

训练搜索使用 15% 均匀先验混合以扩大候选覆盖，实际出招再混入 3% 合法动作探索。陪玩时关闭这两项探索，仍按搜索得到的混合策略采样。

### 职业覆盖

v0.1 使用 `mixed` 训练方式：每十个新对局中，七个从普通初始局面开始，三个从职业专项局面开始。

专项局面由 `build_profession_bank.py` 通过一段完全合法的出招生成，覆盖无职业、术士、炼金术士、炮手、时空、战矛及双方的职业组合。生成过程正常支付资源与回合成本。进入自我对弈后取消职业限制；准备局面的那段出招不作为训练目标。

经验池分为七组：普通开局占采样权重的 70%，六个专项组各占 5%。这样各职业都有训练机会，但不会强制最终自由选职业的比例均匀。

### 参数更新

从经验池采样局面，最小化两个损失之和：

- 策略损失：合法动作上的搜索分布与网络分布之间的交叉熵。
- 价值损失：网络价值与最终胜负标签之间的均方误差。

胜负标签按距离终局的回合数乘以 `0.997^距离`，双方视角符号相反。同时死亡或达到 100 回合上限按和局处理。优化器为 AdamW，学习率 `0.0003`，权重衰减 `0.0001`，梯度范数裁剪为 `1.0`。

v0.1 的训练配置为：32 个并行环境，每次更新先自我对弈 32 步，再训练 24 个大小为 512 的批次；经验池容量 131,072，前 9 次更新只收集数据，第 10 次开始更新参数。训练搜索宽度为 8/4，矩阵求解 64 次，共 1,000 次更新。随附权重保持原样；重新训练使用当前仓库规则。

### 开始新训练

先完成构建，再生成职业局面库：

```sh
python build_profession_bank.py --output curriculum-bank.json --per-pair 64
python train_zero.py --mode mixed --bank curriculum-bank.json --output runs/mixed --device cuda
```

默认生成 36 种有序职业组合，每组 64 个局面，共 2,304 个。训练输出目录必须是新目录。GPU 训练需安装匹配设备的 CUDA 版 PyTorch；CPU 也可以运行，将设备改成 `--device cpu`。

全部参数可通过 `python train_zero.py --help` 查看。例如调整训练规模：

```sh
python train_zero.py --mode mixed --output runs/mixed-small --device cuda --envs 16 --updates 500
```

只从普通开局进行自我对弈时，使用 `--mode free`，不需要生成专项局面库：

```sh
python train_zero.py --mode free --output runs/free --device cuda
```

训练输出说明：

| 生成文件 | 内容 |
|---|---|
| `training-protocol.json` | 参数、随机初始化摘要和源文件哈希 |
| `progress.json` | 更新次数、局数、损失、经验池大小和职业统计 |
| `zero-0100.pt` 等 | 定期保存的网络权重，默认每 100 次更新保存 |
| `selfplay-moves.csv` | 自我对弈动作与结算记录 |
| `starts.jsonl` | 普通开局或专项局面的来源 |
| `terminals.jsonl` | 每盘终局职业、胜负和回合数 |
| `completion.json` | 训练完成摘要 |

将 `config.json` 的 `checkpoint` 改成新权重路径，例如 `runs/mixed/zero-1000.pt`，即可与新模型对战。

## 评测与导出

用相同搜索配置比较两个 Zero 网络，交替双方位置：

```sh
python evaluate.py --first runs/mixed/zero-1000.pt --second checkpoint.pt --games 200 --output runs/mixed-vs-v01.json
```

报告包含胜、负、和、纯胜率、每盘位置与结果，以及两个模型和规则库的哈希。200 局中，每个模型处于两个位置各 100 局。

导出为 C++ 推理格式：

```sh
python export_weights.py runs/mixed/zero-1000.pt runs/mixed/policy.bin
```

`checkpoint.pt` 和 `policy.bin` 是同一网络的两种格式。`neural-policy.hpp` 可独立执行网络前向推理；`predict.cpp` 是它的调用示例：

```sh
build/predict policy.bin < state.txt
```

Windows 使用 `build\predict.exe`。`state.txt` 包含 718 个以空格分隔的局面特征；输出前 156 个数是动作评分，最后一个是局面价值。完整对战入口使用 `play.py`。

Python 单独加载网络：

```python
import torch
from model import load

network = load()
with torch.no_grad():
    logits, value = network(torch.zeros(1, 718))
```

## 文件说明

| 文件 | 用途 |
|---|---|
| `checkpoint.pt` | 随附的 PyTorch 网络权重 |
| `policy.bin` | 同一网络的 C++ 二进制权重 |
| `model.py` | 网络结构、随机初始化与权重加载 |
| `neural-policy.hpp` | 无 PyTorch 依赖的 C++ 网络推理 |
| `predict.cpp` | C++ 单次网络推理示例 |
| `actions.hpp` | 156 个动作的技能与参数映射，参数化技能取 0–16 |
| `play-actions.json` | 命令行使用的动作编号、英文技能名和参数目录 |
| `state-encoder.hpp` | 718 维公开状态编码，含战矛印记、延迟攻击和回调阶段 |
| `game.hpp` | 调用仓库引擎推进对局、检查合法动作、识别终局 |
| `arena-core.cpp` | 批量观察、联合出招和终局自动重置的 C 接口 |
| `tree.hpp` | C++ 搜索的局面复制与批量展开 |
| `arena.cpp` | 汇总规则接口，提供合法出招前缀重放与职业信息 |
| `arena.py` | Python 调用 C++ 动态库的适配层，一个进程共用一个环境池 |
| `search_helpers.py` | 搜索缓冲区、终局赋值和矩阵辅助函数 |
| `search.py` | 两层同时行动搜索、候选采样和叶节点批量推理 |
| `agent.py` | AI 出招与持续对战接口，管理锁定、换位和重新开局 |
| `play.py` | 中文命令行菜单、出招输入和结果显示 |
| `play.cmd` | Windows 双击启动入口 |
| `config.json` | 陪玩和评测的权重、设备、搜索及线程配置 |
| `train_zero.py` | 从零自我对弈训练主程序，支持 `mixed` 和 `free` |
| `training_core.py` | regret matching+ 矩阵求解、终局标签和基础经验池 |
| `profession_replay.py` | 70/30 开局调度及分组经验采样 |
| `build_profession_bank.py` | 用合法动作生成职业专项局面库 |
| `evaluate.py` | 两个冻结 Zero 网络的换位对战评测 |
| `export_weights.py` | 从 PyTorch 权重导出 C++ 权重 |
| `test-agent.cpp` | C++ 动作表、印记及回调编码检查 |
| `test_agent.py` | 推理一致性、权重导出、合法搜索、锁定动作、经验标签及命令行测试 |
| `CMakeLists.txt` | 编译规则动态库、推理示例和 C++ 测试 |
| `build.py` | 跨平台构建入口，Windows 下可自动寻找 w64devkit |
| `requirements.txt` | Python 依赖 |
| `.gitignore` | 排除构建产物、训练结果、生成的局面库及缓存 |
| `README.md` | 使用、训练方法与文件说明 |

## 检查安装

```sh
ctest --test-dir build --output-on-failure
python test_agent.py
```

`build/` 保存本机编译结果，`runs/` 保存新训练和评测结果；它们不会随本目录源码一起提交。
