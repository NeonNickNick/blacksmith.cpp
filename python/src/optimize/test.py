import blacksmith.core as bs
import cma

test = bs.ZeroTest()
scale = bs.BlacksmithZeroParam().to_list()

def evaluate(l:list):
    scaled = [x * y for x, y in zip(l, scale)]
    param = bs.BlacksmithZeroParam()
    param.from_list(scaled)
    test.set_param(param)
    return 0.5 - test.win_rate(50)

x0 = [1 for _ in scale]
sigma0 = 0.2

es = cma.CMAEvolutionStrategy(x0, sigma0, {'bounds': [-2, 2], 'popsize': 40, 'maxiter':40})

while not es.stop():
    solutions = es.ask()                          
    fitness = [evaluate(x) for x in solutions]  
    es.tell(solutions, fitness)                   
    es.disp()   

print('finish')
print(es.result.xbest)
print(es.result.fbest)