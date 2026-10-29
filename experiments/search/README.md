# 搜索对战初步评测

使用同一份 `master-0099.pt`，比较从合法动作策略分布采样的纯网络策略与搜索策略。双方从零资源自由开局，纯网络固定为席位 0、搜索固定为席位 1；双方独立采样，同时提交动作。每种配置 400 局，种子均为 20261029。两组轨迹随策略不同而变化，按独立对局统计。

搜索的根候选宽度为 4、深层宽度为 2，regret matching+ 迭代 32 次，节点预算 200000；模型及搜索在 CPU 上执行，PyTorch 和规则引擎各使用 1 线程。所有搜索均完成请求的深度。原始结果包含逐局结果、局长、耗时和每步完成深度。

| 搜索深度 | 搜索胜 | 纯网络胜 | 和 | 搜索平均每步 | 纯网络平均每步 |
|---|---:|---:|---:|---:|---:|
| 1 回合 | 301 | 99 | 0 | 11.3 ms | 2.8 ms |
| 2 回合 | 277 | 102 | 21 | 41.3 ms | 3.6 ms |

耗时为同一进程内的墙钟时间，包含各自的采样和推理；未对机器负载做严格控制。样本规模较小，两组结果支持继续验证搜索相对纯策略的效果，尚不足以比较两个搜索深度的强弱。

权重 SHA-256：`7f1b1ed2a295e44b34ed0568fd563b014c7636c10f2dc012c1b5c3ef6fa01f5f`；规则 schema：8。复现时在仓库根目录运行：

```powershell
python/.venv/Scripts/python.exe -m master.evaluate_search --checkpoint models/blacksmith-master/master-0099.pt --games 400 --width 4 --leaf-width 2 --depth 1 --iterations 32 --seed 20261029 --output out/search-depth1.json
```

将 `--depth 1` 改为 `--depth 2` 并更换输出路径，可运行第二组。权重和原生绑定需与上面的哈希及 schema 对应。

## 一回合与两回合搜索直接对战

同一权重、零资源自由开局，一回合搜索固定为席位 0，两回合搜索固定为席位 1。两方根宽度 4、深层宽度 2、迭代次数 32、节点预算 200000，种子 20261030，CPU 单线程；共 400 局、9968 回合。全部搜索达到指定深度。

| 一回合搜索胜 | 两回合搜索胜 | 和 | 一回合平均每步 | 两回合平均每步 |
|---:|---:|---:|---:|
| 204 | 186 | 10 | 18.2 ms | 57.8 ms |

按胜 1、和 0.5、负 0 计，一回合搜索得分率为 52.25%，两回合为 47.75%。在这组配置和样本下，结果较接近；两回合搜索耗时更高。逐局结果及完成深度见 [depth1-vs-depth2-400.json](depth1-vs-depth2-400.json)。

```powershell
python/.venv/Scripts/python.exe -m master.evaluate_search --checkpoint models/blacksmith-master/master-0099.pt --games 400 --width 4 --leaf-width 2 --opponent-depth 1 --depth 2 --iterations 32 --seed 20261030 --output experiments/search/depth1-vs-depth2-400.json
```
