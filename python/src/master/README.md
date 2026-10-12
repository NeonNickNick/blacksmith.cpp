# Master

C++ 执行游戏规则、合法性、状态编码和批量分支模拟；Python 执行网络、矩阵搜索、
replay 与自对弈训练。训练仅使用 Master 自对弈。

## 模型和数据

保留的五个模型均为 hidden=256、4 个残差块、history width=64、1 层 Transformer、
4 个注意力头、Sigmoid 价值头。残差分支为 LayerNorm → Linear → SiLU → Linear，
按 `1/sqrt(depth)` 缩放。策略头输出 75 个动作，技能参数最大为 5。

`SCHEMA_VERSION=3`，每个视角的 observation 为 1038 维 float32：

| 区域 | 长度 |
|---|---:|
| 双方公共状态 | 2 × 200 |
| 双方职业状态 | 2 × 238 |
| 回合状态 | 2 |
| 最近八个回合历史 | 8 × 20 |

公共状态包含 HP、MHP、资源、职业、授权技能、攻击及防御；职业状态包含标记、
延迟资源、回调及效果。历史记录双方动作和结算变化，搜索分支复制自己的历史。
旧 MLP/Tanh checkpoint 仍可加载。

价值目标为胜=1、负=0、和=0.5；时间折扣将远期结果向 0.5 收缩。
Sigmoid 使用 logits 交叉熵训练，搜索用 `2*p-1` 转为零和收益。

## 训练

从仓库根目录运行：

```powershell
python/.venv/Scripts/python.exe -m master train --mode free --backbone residual --hidden 256 --depth 4 --history-width 64 --history-layers 1 --history-heads 4 --value-head sigmoid --output out/training
```

`--mode free` 从双方 0 iron、空历史开始。
可选 `--mode mixed --bank PATH` 为 Master 自对弈加入课程起点；课程覆盖五职业
25 个有序组合，抽样权重为自由 40%、课程 60%。课程前缀不是训练目标。

```powershell
python/.venv/Scripts/python.exe -m master curriculum --output out/curriculum.json
```

常用参数：`--envs`、`--horizon`、`--batches`、`--batch-size`、`--replay`、
`--width`、`--leaf-width`、`--regret-iterations`、`--lr`。
GPU 可指定 `--device cuda --precision bf16`；softmax、收益矩阵及损失保持 FP32。

`--max-seconds` 或 `--max-gradient-steps` 达到任一上限便保存并结束。
`--resume PATH` 保留权重、Adam 状态及累计计数，重新构建 replay 和对局；
`--initial PATH` 只载入模型。`--lr-time-offset` 可接续学习率的时间进度。

默认只保存 checkpoint、配置、进度和完成信息，不产生逐步动作与对局追踪文件。
`--record-games` 显式开启 moves CSV、starts JSONL 和 terminals JSONL。

## 本地 play

```powershell
.\play.cmd
.\play.cmd --starting-iron 0 --search-depth 3 --width 24 --leaf-width 4 --search-nodes 200000
```

每次运行只玩一局，均匀随机选择 `models/blacksmith-master/master-01.pt` 至 `master-05.pt` 中一个。
`--checkpoint PATH` 固定权重；`--checkpoint-dir PATH` 指定随机池；`--seed` 可复现抽样。
默认回车或选 `0` 双方 0 iron；`1` 玩家 0.5 iron；`2` 玩家 1 iron，Master 始终为 0。

输入技能名和整数参数，省略参数按 0，名称与 standard_pvp 一致，不区分大小写。
`resign`、`surrender` 或 `认输` 认输，`q` 直接退出，不支持重开。
界面显示 HP/MHP、职业、各项资源及终局真实状态。
每回合输出 Master 自己视角的价值头预测百分比，这是预期得分而非校准的纯胜率。
复盘每个位置也会输出该位置的 Master 网络预测，试玩仍按正常对局显示。

正常终局或认输后询问 `Replay the game? [y/N]`；回车或 `n` 退出，`y` 开始复盘。
复盘位置 N 表示完成 N 个回合，初始位置为 0。每个位置从原始开局（含铁量模式）
重放双方前 N 回合动作构建，完整保留资源、延迟效果和最近八回合历史，不使用 undo。

- `1`：查看玩家和 Master 各自的前三个推荐动作及搜索策略权重。这些权重不是动作胜率。
- `2`：输入 `prev` 前移一回合、`next` 后移一回合；也可直接输入这两个命令。
  位置 0 只能 `next`，最后一个位置只能 `prev`。
- `3`：从当前状态试玩，按正常技能输入与 Master 对战，不能在试玩中前后移动。
- 试玩中的 `4` 或 `back`：回到试玩起始位置；试玩认输或终局也自动返回该位置。

复盘与试玩使用本局原模型，不重新抽权重；试玩动作不会加入原记录。
推荐在独立、可复现的搜索随机流上计算，同一位置重复查看使用缓存。
推荐只从实际搜索的合法候选中排序；不足三个合法候选时显示实际数量。
已触发规则终局的状态不再推荐下一步或开始试玩；认输时的未结算位置可以继续分析或试玩。
对局记录仅保存在本次进程内存中。

`--search-depth` 按双方同时行动的回合计，默认 2；`--width` 控制根部每方候选数，
`--leaf-width` 控制深层候选数，最大均为 75。
`--search-nodes` 为每步迭代加深的总节点预算，预算不足时采用最后完整完成的深度。
`--search-batch` 控制分批展开内存。增大推理搜索不改变模型架构。

## 构建与测试

Windows 用 `build.cmd`，Linux 用 `bash build.sh`；编译扩展后安装到 Python 环境。
可通过 CMake 的 `Python3_EXECUTABLE` 指定 Python 3.11 绑定环境。

```powershell
python/.venv/Scripts/python.exe -m unittest discover -s python/tests -v
ctest --test-dir out/build/native --output-on-failure
```
