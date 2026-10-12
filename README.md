# blacksmith.cpp

C++20 规则引擎、原生 Python 绑定和 Master 神经网络自对弈训练。

## 目录

- `src/`：游戏规则及 C++ 双人终端客户端。
- `python/python-bindings/`：状态编码、批量环境、搜索分支及绑定。
- `python/src/master/models/`：网络定义与 checkpoint 加载。
- `python/src/master/train/`：自对弈、课程、搜索和 replay。
- `python/src/master/play.py`：本地人机对战与赛后复盘。
- `models/blacksmith-master/`：五个 Master 权重，play 每次运行随机选一个。
- `out/build/native/`：本地 C++ 构建产物。
- `tests/`、`python/tests/`：规则、训练和 play 的回归测试。

## 构建

需要 Python 3.11、C++20 编译器、CMake、Ninja 和 uv。
Windows 运行 `build.cmd`，Linux 运行 `bash build.sh`。
当前机器已有可运行的 Python 环境和编译后的绑定。

## 本地对战

```powershell
.\play.cmd
.\play.cmd --starting-iron 0 --search-depth 3 --width 24 --leaf-width 4
```

开局回车或选 `0`：双方 0 iron；选 `1`：玩家 0.5 iron；选 `2`：玩家 1 iron。
Master 始终为 0 iron。输入技能名加参数，如 `iron 0`、`stick 0`、`shield 2`。
省略参数默认为 0，参数范围 0～5。不列出技能列表。
每次运行只玩一局。`resign`（或 `认输`）认输，`q` 直接退出。
正常终局或认输后询问 `Replay the game? [y/N]`，选 `n` 或回车直接退出。
选 `y` 从位置 0 开始复盘：`1` 查看双方搜索推荐前 3 个动作，`2` 按 `prev/next`
移动一回合，`3` 从当前状态试玩。试玩中输入 `4` 或 `back` 返回试玩起点，
不能前后移动；试玩终局或认输也返回起点。复盘中 `q` 退出。
所有位置都从原开局重放双方动作构建；复盘和试玩保留同一个模型，试玩不修改原棋谱。

每回合显示双方 HP、职业、资源和 Master 的网络预测百分比。
该预测为预期得分：胜=1、负=0、和=0.5，并包含训练时间折扣。

## 自对弈训练

```powershell
python/.venv/Scripts/python.exe -m master train --mode free --device cpu --backbone residual --hidden 256 --depth 4 --history-width 64 --history-layers 1 --history-heads 4 --value-head sigmoid --output out/training
```

GPU 可用时改为 `--device cuda --precision bf16`。
`--max-seconds` 和 `--max-gradient-steps` 控制预算，`--resume PATH` 恢复权重与优化器，
`--initial PATH` 只载入权重。默认仅输出 checkpoint 和必要的训练进度、配置；
需要逐步对局记录时才加 `--record-games`。

网络、协议及参数见 [Master 文档](python/src/master/README.md)。
