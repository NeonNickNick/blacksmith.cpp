import json
from pathlib import Path

import blacksmith.core as bs

test = bs.ZeroTest()
script_dir = Path(__file__).resolve().parent
file_path = script_dir / 'data.txt'
pl = json.loads(file_path.read_text(encoding='utf-8'))

test_param= bs.BlacksmithZeroParam()
test_param.from_list(pl)
test_param.operate_mode()
test.set_test_param(test_param)

print(test.win_rate(100))

lancer_test = bs.LancerTest()
lancer_test.set_test_param(test_param)
print(lancer_test.win_rate(100))