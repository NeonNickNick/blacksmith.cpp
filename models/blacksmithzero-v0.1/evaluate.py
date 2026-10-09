"""Balanced-seat evaluation of two frozen Zero checkpoints."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import numpy as np
import torch
from agent import Agent, configuration
from arena import ProfessionArena, LIBRARY


def main(args):
    if args.games < 2 or args.games % 2:
        raise ValueError('games must be positive and even')
    cfg=configuration()
    torch.manual_seed(args.seed)
    first=Agent(args.first,cfg,args.seed)
    second=Agent(args.second,cfg,args.seed+1)
    arena=ProfessionArena(1,cfg['engine_threads'],cfg['root_width'])
    records=[]
    start=time.monotonic()
    for game in range(args.games):
        seat=game%2
        while True:
            # Both decisions read the same state; neither receives the other move.
            a=first.choose(arena,seat)
            b=second.choose(arena,1-seat)
            moves=np.zeros((1,2),np.int32)
            moves[0,seat],moves[0,1-seat]=a,b
            arena.step(moves)
            outcome=int(arena.results[0,0])
            if outcome:
                result=0 if outcome>=3 else 1 if outcome==seat+1 else -1
                records.append({'game':game+1,'seat':seat,'result':result,'outcome':outcome,'rounds':int(arena.results[0,1])})
                print(json.dumps(records[-1]),flush=True)
                break
    wins=sum(r['result']==1 for r in records)
    losses=sum(r['result']==-1 for r in records)
    report={'games':args.games,'wins':wins,'losses':losses,'draws':args.games-wins-losses,
            'win_rate':wins/args.games,'seconds':time.monotonic()-start,'seed':args.seed,
            'settings':cfg,'first_sha256':hashlib.sha256(Path(args.first).read_bytes()).hexdigest(),
            'second_sha256':hashlib.sha256(Path(args.second).read_bytes()).hexdigest(),
            'library_sha256':hashlib.sha256(LIBRARY.read_bytes()).hexdigest(),'results':records}
    destination=Path(args.output)
    destination.parent.mkdir(parents=True,exist_ok=True)
    destination.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k!='results'}),flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--first',required=True)
    parser.add_argument('--second',required=True)
    parser.add_argument('--games',type=int,default=200)
    parser.add_argument('--seed',type=int,default=20261009)
    parser.add_argument('--output',default='runs/evaluation.json')
    main(parser.parse_args())
