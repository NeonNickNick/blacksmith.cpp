import json
from pathlib import Path

import blacksmith.core as bs
import cma

test = bs.ZeroTest()
script_dir = Path(__file__).resolve().parent
file_path = script_dir / 'data.txt'
pl = json.loads(file_path.read_text(encoding='utf-8'))

baseline_param= bs.BlacksmithZeroParam()
baseline_param.from_list(pl)
baseline_param.optimize_mode()
test.set_baseline_param(baseline_param)

scale = baseline_param.to_list()

def evaluate(l:list):
    scaled = [x * y for x, y in zip(l, scale)]
    param = bs.BlacksmithZeroParam()
    param.from_list(scaled)
    param.optimize_mode()
    test.set_test_param(param)
    return 0.5 - test.win_rate(400)

x0 = [1 for _ in scale]
sigma0 = 0.2

es = cma.CMAEvolutionStrategy(x0, sigma0, {'bounds': [-2, 2], 'popsize': 32, 'maxiter':20})

while not es.stop():
    solutions = es.ask()                          
    fitness = [evaluate(x) for x in solutions]  
    es.tell(solutions, fitness)                   
    es.disp()   

print('finish')
print(0.5 - es.result.fbest)

scaled = [x * y for x, y in zip(es.result.xbest, scale)] # type: ignore

script_dir = Path(__file__).resolve().parent
file_path = script_dir / 'data.txt'
file_path.write_text(json.dumps(scaled), encoding='utf-8')