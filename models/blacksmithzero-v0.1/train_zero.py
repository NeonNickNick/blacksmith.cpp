"""Fresh Zero: paired free-start control and 70/30 legal-start curriculum.

No checkpoint loading, human examples, opponent models, or shaped rewards.
Scenario prefixes are excluded from both policy and value training targets.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import time
import numpy as np
import torch
from model import Policy
from arena import ProfessionArena, ROLES, ROOT, LIBRARY
from profession_replay import Starts, StratifiedReplay
from search import search_tree


def main(args):
    torch.set_num_threads(1)
    torch.manual_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = torch.device(args.device)
    model = Policy().to(device)
    optimizer = torch.optim.AdamW(model.parameters(), lr=args.lr, weight_decay=1e-4)
    arena = ProfessionArena(args.envs, args.threads, args.width)
    replay = StratifiedReplay(args.mode, args.replay)
    starts = Starts(args.mode, args.bank, args.seed+1)
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=False)
    sources = [*ROOT.glob('*.py'), *ROOT.glob('*.hpp'), *ROOT.glob('*.cpp'), LIBRARY]
    sources += list((ROOT.parents[1]/'src/blacksmith-core').rglob('*.cpp'))
    sources += list((ROOT.parents[1]/'src/blacksmith-core').rglob('*.hpp'))
    manifest = {'config': vars(args), 'random_initialization': True, 'empty_replay': True,
                'old_weights_loaded': False, 'human_data_used': False, 'opponents_loaded': False,
                'policy_targets': 'unrestricted search from actual public state',
                'value_targets': 'terminal outcome, original discount 0.997',
                'prefixes_used_as_training_targets': False,
                'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                'bank_sha256': hashlib.sha256(Path(args.bank).read_bytes()).hexdigest() if args.mode=='mixed' else None}
    (output/'training-protocol.json').write_text(json.dumps(manifest,indent=2))
    initial = hashlib.sha256(b''.join(v.detach().cpu().numpy().tobytes() for v in model.state_dict().values())).hexdigest()
    manifest['initial_weights_sha256'] = initial
    (output/'training-protocol.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps(manifest),flush=True)
    moves = (output/'selfplay-moves.csv').open('w',buffering=1048576)
    moves.write('update,env,game,round,first_action,second_action,outcome,first_hp,second_hp\n')
    reset_log = (output/'starts.jsonl').open('w',buffering=1048576)
    terminal_log = (output/'terminals.jsonl').open('w',buffering=1048576)
    histories = [[] for _ in range(args.envs)]
    groups = [-1]*args.envs
    generations = [0]*args.envs
    started_counts, finished_counts = collections.Counter(), collections.Counter()
    observed_roles, terminal_roles = collections.Counter(), collections.Counter()

    def reset(env):
        group, entry = starts.next()
        groups[env] = group
        arena.reset(env, entry['prefix'] if entry else [])
        started_counts[str(group)] += 1
        reset_log.write(json.dumps({'env':env,'game':generations[env],'group':group,
                                   'bank_id':entry['id'] if entry else None})+'\n')

    for env in range(args.envs):
        reset(env)
    arena.refresh()
    games = gradients = 0
    start = time.monotonic()
    for update in range(1,args.updates+1):
        model.eval()
        search_start = time.monotonic()
        for _ in range(args.horizon):
            choices, policies = search_tree(model,arena,rng,device,leaf_width=args.leaf_width,
                                            iterations=args.regret_iterations,exploration=.15)
            roles_before = arena.info[:,1:].copy()
            for env in range(args.envs):
                histories[env].append((arena.observations[env].copy(),arena.masks[env].copy(),policies[env].copy()))
                observed_roles.update(str(int(role)) for role in roles_before[env])
            arena.step(choices)
            # zs_step resets terminal games; copy outcomes before replacing any start.
            outcomes = arena.results.copy()
            for env, result in enumerate(outcomes):
                moves.write(f'{update},{env},{generations[env]},{int(result[1])},{choices[env,0]},{choices[env,1]},{int(result[0])},{result[2]},{result[3]}\n')
                if not result[0]:
                    continue
                final_roles = roles_before[env].tolist()
                for seat in range(2):
                    for role in range(1,6):
                        if choices[env,seat] == arena.actions[role]:
                            final_roles[seat] = role
                    if choices[env,seat] == arena.actions[6]:
                        final_roles[seat] = 5  # Existing bloodsigil-path rule, unchanged.
                terminal_roles.update(str(role) for role in final_roles)
                terminal_log.write(json.dumps({'update':update,'env':env,'game':generations[env],
                    'group':groups[env],'roles':final_roles,'outcome':int(result[0]),'rounds':int(result[1])})+'\n')
                replay.add(groups[env],histories[env],int(result[0]))
                histories[env].clear()
                finished_counts[str(groups[env])] += 1
                games += 1
                generations[env] += 1
                reset(env)
            arena.refresh()
        search_seconds = time.monotonic()-search_start
        model.train()
        losses = []
        if update >= args.warmup:
            if not replay.ready(args.batch_size):
                raise RuntimeError('Replay strata not ready at predeclared warmup; stop instead of silently changing mixture')
            for _ in range(args.batches):
                states,masks,policies,values,weights = replay.sample(rng,args.batch_size)
                x,mask,target_policy,target_value,weight = [torch.as_tensor(a,device=device) for a in [states,masks,policies,values,weights]]
                logits,value = model(x)
                logp = logits.masked_fill(mask==0,-1e9).log_softmax(-1)
                pl = (-(target_policy*logp).sum(-1)*weight).sum()
                vl = ((value-target_value).square()*weight).sum()
                loss = pl+vl
                if not torch.isfinite(loss):
                    raise RuntimeError('Nonfinite loss')
                optimizer.zero_grad(set_to_none=True)
                loss.backward()
                torch.nn.utils.clip_grad_norm_(model.parameters(),1.0)
                optimizer.step()
                gradients += 1
                losses.append([pl.item(),vl.item()])
        elapsed = time.monotonic()-start
        status = {'update':update,'games':games,'gradient_steps':gradients,'elapsed_seconds':elapsed,
                  'search_seconds':search_seconds,'loss':np.mean(losses,axis=0).tolist() if losses else [],
                  'pool_sizes':replay.sizes(),'started':dict(started_counts),'finished':dict(finished_counts),
                  'observed_roles':dict(observed_roles),'terminal_roles':dict(terminal_roles)}
        print(json.dumps(status),flush=True)
        (output/'progress.json').write_text(json.dumps(status,indent=2))
        if update%args.save_every==0 or update==args.updates:
            checkpoint = output/f'zero-{update:04d}.pt'
            torch.save({'model':model.state_dict(),'features':718,'actions':156,'hidden':256,
                        'update':update,'games':games,'config':vars(args)},checkpoint)
            for handle in [moves,reset_log,terminal_log]:
                handle.flush()
        if elapsed > args.max_seconds and update != args.updates:
            raise RuntimeError('Safety time cap reached; matched-budget experiment incomplete')
    for handle in [moves,reset_log,terminal_log]:
        handle.close()
    status.update({'checkpoint':checkpoint.name,'complete':True,'initial_weights_sha256':initial,
                   'unfinished_games_excluded_from_replay':sum(bool(h) for h in histories)})
    (output/'completion.json').write_text(json.dumps(status,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--mode',choices=['free','mixed'],required=True)
    p.add_argument('--output',required=True)
    p.add_argument('--bank',default='curriculum-bank.json')
    for name,default in [('envs',32),('threads',12),('width',8),('leaf-width',4),('horizon',32),
                         ('updates',1000),('batches',24),('batch-size',512),('replay',131072),
                         ('regret-iterations',64),('seed',2026100841),('save-every',100),('warmup',10)]:
        p.add_argument('--'+name,type=int,default=default)
    p.add_argument('--lr',type=float,default=.0003)
    p.add_argument('--device',default='cuda')
    p.add_argument('--max-seconds',type=float,default=10800)
    import os
    os.chdir(ROOT)
    main(p.parse_args())
