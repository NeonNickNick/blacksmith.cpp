import blacksmith.core as bs
import numpy as np

test = bs.ZeroTest()
param = bs.BlacksmithZeroParam()
param.optimize_mode()
test.set_baseline_param(param)
test.set_test_param(param)
res = np.zeros(10, dtype='float')
for i in range(10):
    res[i] = test.win_rate(400)
    print(i)
print(res.std())