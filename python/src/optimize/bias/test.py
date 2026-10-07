import json
import time
from pathlib import Path

import blacksmith.core as bs
from skopt import gp_minimize
from skopt.learning import GaussianProcessRegressor
from skopt.learning.gaussian_process.kernels import ConstantKernel, Matern
from skopt.space import Real
from skopt.utils import use_named_args

test = bs.ZeroTest()
lancer_test = bs.LancerTest()
script_dir = Path(__file__).resolve().parent
file_path = script_dir / 'data.txt'
pl = json.loads(file_path.read_text(encoding='utf-8'))

baseline_param= bs.BlacksmithZeroParam()
baseline_param.from_list(pl)
baseline_param.optimize_mode()
test.set_baseline_param(baseline_param)

scale = baseline_param.to_list()

# ============ 1. 搜索空间：19 维连续 [-2, 2] ============
DIM = 19
space = [Real(-10.0, 10.0, name=f"x{i}", prior="uniform") for i in range(DIM)]

# ============ 2. 你的评估函数（耗时操作） ============
def evaluate(l:list):
    scaled = [x * y for x, y in zip(l, scale)]
    param = bs.BlacksmithZeroParam()
    param.from_list(scaled)
    param.optimize_mode()
    test.set_test_param(param)
    lancer_test.set_test_param(param)
    standard_win_rate = test.win_rate(400)
    lancer_win_rate = lancer_test.win_rate(400)
    return -2.0 / (1.0 / (standard_win_rate + 1e-6) + 1.0 / (lancer_win_rate + 1e-6))
    # ==================================

# ============ 3. 包装成 skopt 目标函数 ============
# 记录所有评估历史，便于事后分析/复现
history = {"x": [], "y": [], "t": []}

@use_named_args(space)
def objective(**kwargs):
    x = [kwargs[d.name] for d in space] # type: ignore
    t0 = time.time()
    y = evaluate(x)
    history["x"].append(x)
    history["y"].append(y)
    history["t"].append(time.time() - t0)
    print(f"[eval {len(history['y']):3d}] y={y:.6f}  ({history['t'][-1]:.1f}s)")
    return y

# ============ 4. 自定义 GP：Matern + ARD ============
# 19 维必须开启 ARD（每维一个 length_scale），否则 GP 会欠拟合
kernel = ConstantKernel(1.0, (1e-3, 1e3)) * Matern(
    length_scale=[0.5] * DIM,              # 每维独立
    length_scale_bounds=(1e-2, 1e1),
    nu=2.5,
)
base_estimator = GaussianProcessRegressor(
    kernel=kernel,
    normalize_y=True,                       # 各问题 y 量级不同，建议开
    alpha=1e-4,                             # 噪声很小；若 evaluate 有随机性调大
    n_restarts_optimizer=2,                 # 每轮多起点优化超参，提升拟合
)

# ============ 5. 运行贝叶斯优化 ============
if __name__ == "__main__":
    N_INIT = 19        # 初始随机点：约等于维度数，别太多
    N_CALLS = 50      # 总预算：19 初始 + 81 贝叶斯迭代

    result = gp_minimize(
        func=objective,
        dimensions=space,
        base_estimator=base_estimator,
        n_calls=N_CALLS,
        n_initial_points=N_INIT,
        acq_func="EI",              # "EI" / "LCB" / "PI"
        acq_optimizer="lbfgs",      # 连续空间用梯度优化采集函数
        n_jobs=1,                   # 明确串行
        random_state=42,
        verbose=False,
    )

    print("\n===== 最优结果 =====")
    assert result is not None
    print("最优分数:", -result.fun)
    print("最优参数:")
    for i, v in enumerate(result.x):
        print(f"  x{i} = {v:+.4f}")

    # 保存结果
scaled = [x * y for x, y in zip(result.x, scale)] 

script_dir = Path(__file__).resolve().parent
file_path = script_dir / 'data.txt'
file_path.write_text(json.dumps(scaled), encoding='utf-8')