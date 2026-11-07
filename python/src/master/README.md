# Master

C++ 执行游戏规则、合法性、状态编码和批量分支模拟；Python 执行网络、矩阵搜索、
replay 与自对弈训练。训练仅使用 Master 自对弈。

## 模型和数据

当前模型采用 hidden=256、4 个残差块、history width=64、1 层 Transformer、
4 个注意力头、Sigmoid 价值头。残差分支为 LayerNorm → Linear → SiLU → Linear，
按 `1/sqrt(depth)` 缩放。旧版默认权重与当前模型不兼容。

各个 Python 文件的职责和阅读顺序见 [Python 文件索引](../../README.md#每个-python-文件的作用)。

当前 `SCHEMA_VERSION=11`，每个视角的 observation 为 1284 维 float32。
底座策略头输出 126 个普通技能动作评分和价值。动作表还包含 126 个联想动作，
只在幻影书专用模型启用且规则允许时参与搜索。底座阶段屏蔽联想。

每个根职业有独立的可训练模型，输入为当前状态编码、底座策略评分及价值 logits，
输出对评分和价值 logits 的修正。幻影书模型额外输出联想动作的修正；初始修正全为零，
即从底座策略和价值开始。无职业或尚未训练的职业使用底座。模型按自身职业路由，
双方视角、搜索分支及回放均使用同一规则。底座的输入始终屏蔽联想授权，
历史中的联想动作以被复制技能的编号编码。

checkpoint 使用 `architecture=routed-profession-v1`，保存底座、所有职业模型及已启用职业。
旧架构和旧 schema 的权重需要重新训练。`--initial` 和 `--resume` 沿用 checkpoint 中的配置。

| 区域 | 长度 |
|---|---:|
| 双方公共状态 | 2 × 234 |
| 双方职业状态 | 2 × 327 |
| 回合状态 | 2 |
| 最近八个回合历史 | 8 × 20 |

公共状态包含 HP、MHP、资源、职业、授权技能、攻击及防御；职业状态包含标记、
延迟资源、回调、效果及当前启用职业的资源。历史记录双方完整动作编号和结算变化，
搜索分支复制自己的历史。
职业身份只记录互斥的根节点职业，不包含炼药锅和炼金术等副装备；这些技能包的
权限仍编码在授权技能中。炼药锅从根节点获取，炼金术从术士节点获取。
checkpoint 的 schema、输入和输出维度必须与当前绑定一致。

价值目标为胜=1、负=0、和=0.5；时间折扣将远期结果向 0.5 收缩。
Sigmoid 使用 logits 交叉熵训练，搜索用 `2*p-1` 转为零和收益。

## 训练

从仓库根目录运行：

```powershell
python/.venv/Scripts/python.exe -m master train --mode free --hidden 256 --depth 4 --history-width 64 --history-layers 1 --history-heads 4 --output out/training
```

训练分两阶段。先训练底座，不指定 `--role`：

```powershell
python/.venv/Scripts/python.exe -m master train --mode free --output out/base
```

再以底座 checkpoint 为起点，逐个训练职业模型。每次只更新指定职业模型，底座和其他职业模型冻结；
把上一阶段输出的 checkpoint 传给下一阶段，以累积全部职业模型：

```powershell
python/.venv/Scripts/python.exe -m master train --mode free --role warlock --initial out/base/master-1000.pt --output out/warlock
python/.venv/Scripts/python.exe -m master train --mode free --role phantombook --initial out/warlock/master-1000.pt --output out/phantombook
```

其余职业依次使用 `cannon`、`driver`、`lancer`、`bloodsigil`、`crossbow`。
`--resume` 只能继续相同阶段和职业；`--initial` 用于切换阶段或职业，重新开始优化器。
自由开局中指定职业出现较少时，可使用覆盖目标职业的混合课程加快采样。

`--mode free` 从双方 0 iron、空历史开始。
`--mode phantombook --bank PATH` 使用 50% 自由开局和 50% 幻影书专项开局，
replay 也按 50/50 分层抽样；专项均衡覆盖幻影书与其他启用职业的双方席位组合。
可用 `python -m master curriculum --focus-role phantombook --output PATH` 生成专项开局。
可选 `--mode mixed --bank PATH` 为 Master 自对弈加入课程起点；课程自动覆盖所有启用职业，
目前为七职业的 49 个有序组合，抽样权重为自由 40%、课程 60%。课程前缀不是训练目标。
当前混合训练最小 batch 为 82，最小 replay 容量为 5227；旧课程库需要重新生成。

```powershell
python/.venv/Scripts/python.exe -m master curriculum --output out/curriculum.json
```

常用参数：`--envs`、`--horizon`、`--batches`、`--batch-size`、`--replay`、
`--width`、`--leaf-width`、`--regret-iterations`、`--lr`。
GPU 可指定 `--device cuda --precision bf16`；softmax、收益矩阵及损失保持 FP32。

`--max-seconds` 或 `--max-gradient-steps` 达到任一上限便保存并结束。
`--resume PATH` 保留权重、Adam 状态及累计计数，重新构建 replay 和对局；
`--initial PATH` 只载入模型。`--lr-time-offset` 可接续学习率的时间进度。
`--updates 0 --max-seconds 0 --max-gradient-steps 0` 持续训练到收到中断信号，
中断后保存最终 checkpoint。若需固定学习率，可将 `--lr` 和 `--min-lr` 设为相同值。

默认只保存 checkpoint、配置、进度和完成信息，不产生逐步动作与对局追踪文件。
`--record-games` 显式开启 moves CSV、starts JSONL 和 terminals JSONL。

## 本地 play

```powershell
.\play.cmd
.\play.cmd --starting-iron 0 --search-depth 3 --width 24 --leaf-width 4 --search-nodes 200000
```

每次运行只玩一局，默认加载 `models/blacksmith-master/master-0099.pt`。
`--checkpoint PATH` 固定权重；`--checkpoint-dir PATH` 指定随机池；`--seed` 可复现抽样。
默认回车或选 `0` 双方 0 iron；`1` 玩家 0.5 iron；`2` 玩家 1 iron，Master 始终为 0。

输入 `skill param`，如 `magicattack 3`；幻影书也可输入 `association 0 magicattack 3`。
单个技能省略参数按 0，名称与 standard_pvp 一致，不区分大小写，允许空格和制表符。
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
`--leaf-width` 控制深层候选数，最大均为当前动作总数 252。
`--search-nodes` 为每步迭代加深的总节点预算，预算不足时采用最后完整完成的深度。
`--search-batch` 控制分批展开内存。增大推理搜索不改变模型架构。

## 构建与测试

Windows 用 `build.cmd`，Linux 用 `bash build.sh`；编译扩展后安装到 Python 环境。
可通过 CMake 的 `Python3_EXECUTABLE` 指定 Python 3.11 绑定环境。

```powershell
python/.venv/Scripts/python.exe -m unittest discover -s python/tests -v
ctest --test-dir out/build/native --output-on-failure
```
