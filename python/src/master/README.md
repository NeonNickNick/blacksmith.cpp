# Master

C++ 执行游戏规则、合法性、状态编码和批量分支模拟；Python 执行网络、矩阵搜索、
replay 与自对弈训练。训练仅使用 Master 自对弈。

## 模型和数据

当前模型采用 hidden=256、4 个残差块、history width=64、1 层 Transformer、
4 个注意力头、Sigmoid 价值头。残差分支为 LayerNorm → Linear → SiLU → Linear，
按 `1/sqrt(depth)` 缩放。当前本地默认权重 `master-0099.pt` 使用 schema 8；
暂停后下载的 `master-0117.pt` 独立保存在 `out/cloud-training` 的结果目录中，没有替换默认权重。
旧 schema 3（75 个动作）权重不兼容当前架构。

各个 Python 文件的职责和阅读顺序见 [Python 文件索引](../../README.md#每个-python-文件的作用)。

当前 `SCHEMA_VERSION=8`，策略头输出 266 个动作，每个视角的 observation 为
1458 维 float32。Monk、Prophet 暂时通过注释停用。一般技能参数为 0～5，格挡为 1～4。
动作表包含 133 个普通动作和对应的 133 个联想动作；不保留缺少后续技能的裸联想。
`ACTIONS` 的每一项是 `((skill, param), ...)` 技能链，`ACTION_NAMES` 提供对应文本，
`parse_action` 将文本转为动作编号，`action_index` 可接受根节点及可选的下一节点技能与参数。

模型固定采用残差主干、历史 Transformer、association 策略头和 Sigmoid 价值头。
默认 hidden=256、4 个残差块、history width=64、1 层 Transformer、4 个注意力头。
历史输入始终参与计算，`--history-layers` 必须大于 0；宽度、层数及注意力头数仍可调整。
基础技能头给出 133 个评分 `b`；联想分支给出
133 个残差 `r` 和一个门控评分 `g`，合成为 `[b, b+r+g]` 的 266 个完整动作评分。
对完整动作应用同一个原生合法性掩码和 softmax，不分别选技能和参数，以免组合出非法动作。
历史嵌入保留完整动作 ID，同时共享对应基础技能的嵌入，让普通用法与联想用法能相互学习。
这保持搜索与 replay 的动作接口不变。checkpoint 必须记录
`architecture=residual-history-association-v1`，不提供旧架构兼容加载或架构切换参数。
`--initial` 和 `--resume` 沿用 checkpoint 中的宽度和层数。

| 区域 | 长度 |
|---|---:|
| 双方公共状态 | 2 × 266 |
| 双方职业状态 | 2 × 382 |
| 回合状态 | 2 |
| 最近八个回合历史 | 8 × 20 |

公共状态包含 HP、MHP、资源、职业、授权技能、攻击及防御；职业状态包含标记、
延迟资源、回调、效果及当前启用职业的资源。历史记录双方完整动作编号和结算变化，
包含联想的后续动作；搜索分支复制自己的历史。已停用职业的专属资源和分身不占输入通道。
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

`--mode free` 从双方 0 iron、空历史开始。
`--mode phantombook --bank PATH` 使用 50% 自由开局和 50% 幻影书专项开局，
replay 也按 50/50 分层抽样；专项均衡覆盖幻影书与其他启用职业的双方席位组合。
可用 `python -m master curriculum --focus-role phantombook --association-ready --output PATH`
生成合法前缀起点，使至少一名幻影书玩家拥有可用的 association 动作；不把前缀当作学习目标，
也不强迫模型在后续对局中选择该动作。专项排除同职业镜像，因为借用技能主要需要不同职业对手。
可选 `--mode mixed --bank PATH` 为 Master 自对弈加入课程起点；课程自动覆盖所有启用职业，
目前为八职业的 64 个有序组合，抽样权重为自由 40%、课程 60%。课程前缀不是训练目标。
当前混合训练最小 batch 为 107，最小 replay 容量为 6827；旧课程库需要重新生成。

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

每次运行只玩一局，默认加载 `models/blacksmith-master/` 中的 `.pt` 权重；当前仅保留新训练的 `master-0099.pt`。
`--checkpoint PATH` 固定权重；`--checkpoint-dir PATH` 指定随机池；`--seed` 可复现抽样。
默认回车或选 `0` 双方 0 iron；`1` 玩家 0.5 iron；`2` 玩家 1 iron，Master 始终为 0。

输入 `skill param [skill param ...]`，如 `association 0 magicattack 3`。
单个技能省略参数按 0，名称与 standard_pvp 一致，不区分大小写，允许空格和制表符。
联想消耗 2 Dream，免除复制技能的成本；要求对手当前能使用该技能且自己不能使用，不可复制联想自身。
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
`--leaf-width` 控制深层候选数，最大均为当前动作总数 266。
`--search-nodes` 为每步迭代加深的总节点预算，预算不足时采用最后完整完成的深度。
`--search-batch` 控制分批展开内存。增大推理搜索不改变模型架构。

## 构建与测试

Windows 用 `build.cmd`，Linux 用 `bash build.sh`；编译扩展后安装到 Python 环境。
可通过 CMake 的 `Python3_EXECUTABLE` 指定 Python 3.11 绑定环境。

```powershell
python/.venv/Scripts/python.exe -m unittest discover -s python/tests -v
ctest --test-dir out/build/native --output-on-failure
```
