# Blacksmith Python

`src/master/models` 定义网络，`src/master/train` 实现 Master 自对弈训练，
`src/master/play.py` 用于本地人机对战。规则和批量模拟来自 C++ 原生绑定。

构建及命令见 [项目说明](../README.md)，协议和参数见 [Master 文档](src/master/README.md)。

## 每个 Python 文件的作用

下面列出项目维护的全部 18 个 `.py` 文件，不包含 `.venv`、构建输出和第三方依赖。
路径均相对于本目录。

### 命令入口、对战与公共工具

| 文件 | 职责与主要接口 |
|---|---|
| [src/master/__init__.py](src/master/__init__.py) | `main()` 注册 `curriculum`、`train`、`play` 子命令，由各模块的 `configure()` 定义参数并选择实际执行函数。也是安装后的 `master` 命令入口。 |
| [src/master/__main__.py](src/master/__main__.py) | 将 `python -m master` 转交给同一个 `main()`，保持模块运行和安装命令使用相同的参数解析。 |
| [src/master/runtime.py](src/master/runtime.py) | 将命令参数转为可保存配置，计算文件 SHA256，并以临时文件替换的方式写入 JSON 进度及协议。 |
| [src/master/agent.py](src/master/agent.py) | `Agent` 加载冻结模型，封装落子、价值头预测及双方推荐分析；复盘分析使用独立随机流，不消耗正式对局的落子随机流。 |
| [src/master/search.py](src/master/search.py) | 本地 play/复盘的迭代加深搜索。递归展开双方同时行动的收益矩阵，限制节点总量和展开批次；预算不足时返回最后完整完成的深度及 `SearchInfo`。 |
| [src/master/play.py](src/master/play.py) | 命令行单局对战、铁量开局、技能输入、认输、状态显示、复盘和试玩。`CheckpointPool` 选择权重；`GameRecord` 保存动作并从原始开局重建指定回合。 |

### 模型与数据约定

| 文件 | 职责与主要接口 |
|---|---|
| [src/master/models/__init__.py](src/master/models/__init__.py) | 集中导出 `ModelConfig`、`Policy`、`checkpoint()`、`load()`、`masked_log_probs()`，为训练和推理提供统一导入入口。 |
| [src/master/models/data.py](src/master/models/data.py) | 定义 NumPy 数组、单局训练历史和开局前缀的类型别名；说明这些结构在原生模拟、训练和 replay 之间的含义。 |
| [src/master/models/net.py](src/master/models/net.py) | 定义配置校验、历史 Transformer、残差主干、普通/association 策略头及 Sigmoid 价值头；提供合法动作归一化、checkpoint 打包及严格的架构/schema 加载检查。 |

### 自对弈训练

| 文件 | 职责与主要接口 |
|---|---|
| [src/master/train/__init__.py](src/master/train/__init__.py) | 标记训练子包及其用途；导入该文件不会启动训练。 |
| [src/master/train/sampling.py](src/master/train/sampling.py) | 定义职业组合和目标采样质量：`free` 全自由、`mixed` 自由40%/课程60%、`phantombook` 自由50%/专项50%；提供对应组合列表和最小混合容量。 |
| [src/master/train/curriculum.py](src/master/train/curriculum.py) | 通过原生规则模拟生成合法开局前缀。可指定职业，并让 phantombook 专项起点提供合法 association 选项；保存的是起点，不是示范策略标签。 |
| [src/master/train/replay.py](src/master/train/replay.py) | `Replay` 将完整终局轨迹转为双方视角、带时间折扣的策略/价值样本；`StratifiedReplay` 维护分层环形缓冲和采样权重；`Starts` 按配比选择空开局或课程前缀。 |
| [src/master/train/search.py](src/master/train/search.py) | 训练用固定两回合搜索，以及本地搜索复用的矩阵求解工具。包含合法候选抽样、regret matching+、终局收益覆盖和候选策略映射回完整动作空间。 |
| [src/master/train/train.py](src/master/train/train.py) | 编排并行自对弈、终局轨迹入库、搜索策略监督、价值损失、AdamW 更新和保存。负责恢复模型/优化器、累计计数、预算或信号停止，以及可选的真实对局日志。 |

### 原生包安装与测试

| 文件 | 职责与主要接口 |
|---|---|
| [blacksmith/setup.py](blacksmith/setup.py) | 将包含 CMake 编译扩展的 `blacksmith` wheel 标记为平台相关二进制包；不在这里编译 C++ 或重新实现游戏规则。 |
| [tests/test_master.py](tests/test_master.py) | 检查原生动作/schema、职业合法性、association、历史与分支隔离、网络梯度、checkpoint、训练停止/恢复、课程和 replay。含按环境跳过的 CUDA 检查及暂时停用的 Prophet 用例。 |
| [tests/test_play.py](tests/test_play.py) | 检查搜索深度/预算、模型池、命令行交互、开局资源、预测视角、认输、复盘边界和试玩恢复。使用可控的假搜索树及临时权重文件，避免依赖发布模型的数量。 |

## 从哪里开始阅读

训练主链路：`__init__.py` → `train/train.py` → `Starts` / `Arena` →
`train/search.py` → `Policy` → `Replay` → 梯度更新与 checkpoint。
`Arena` 和 `SearchTree` 来自 C++ 绑定，Python 不另维护一套规则。

本地对战主链路：`play.py` → `Agent` → `search.py` → `Policy`。
复盘移动不会修改已有局面来“撤销”，而是由 `GameRecord.state_at()` 从原始开局重放动作；
试玩只修改这次重建的局面，不写回原始对局记录。

两个 `search.py` 的区别：训练版固定展开两回合，以便批量采样；本地版按完整回合逐层加深，
在节点预算内分析更深局面。两者都先求双方同时出招的混合策略，再从策略抽样动作，
不把这款游戏当成轮流落子的游戏。

状态编码布局、技能链、价值目标、训练参数及完整 play 操作见
[Master 文档](src/master/README.md)。原生 wheel 的职责见
[绑定包说明](blacksmith/README.md)。
