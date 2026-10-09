"""Frozen network + simultaneous search; persistent local matches."""
import hashlib
import json
from pathlib import Path
import secrets
import time
import numpy as np
import torch
from arena import ProfessionArena, ROOT, LIBRARY
from model import load
from search import search_tree


def configuration():
    settings = json.loads((ROOT/'config.json').read_text(encoding='utf-8'))
    for key in ('root_width', 'leaf_width'):
        if not 1 <= settings[key] <= 156:
            raise ValueError(f'{key} must be between 1 and 156')
    for key in ('regret_iterations','torch_threads','engine_threads'):
        if settings[key] < 1:
            raise ValueError(f'{key} must be positive')
    return settings


class Agent:
    def __init__(self, checkpoint=None, settings=None, seed=None):
        self.settings = settings or configuration()
        self.device = torch.device(self.settings['device'])
        torch.set_num_threads(self.settings['torch_threads'])
        self.checkpoint = Path(checkpoint) if checkpoint else ROOT/self.settings['checkpoint']
        self.model = load(self.checkpoint).to(self.device).eval()
        self.rng = np.random.default_rng(seed if seed is not None else secrets.randbits(64))

    def choose(self, arena, seat):
        choices, _ = search_tree(self.model, arena, self.rng, self.device,
            leaf_width=self.settings['leaf_width'], iterations=self.settings['regret_iterations'])
        return int(choices[0, seat])


class Match:
    def __init__(self):
        self.agent = Agent()
        self.catalog = json.loads((ROOT/'play-actions.json').read_text(encoding='utf-8'))
        self.metadata = {'model':'blacksmithzero-v0.1', 'rules_version':'repository-engine',
                         'checkpoint_sha256':hashlib.sha256(self.agent.checkpoint.read_bytes()).hexdigest(),
                         'library_sha256':hashlib.sha256(LIBRARY.read_bytes()).hexdigest(),
                         'device':str(self.agent.device)}
        self.game = 0
        self.finished = True
        self.committed = None
        self.think_seconds = 0

    def choose(self):
        start = time.perf_counter()
        self.committed = self.agent.choose(self.arena, self.ai_seat)
        self.think_seconds = time.perf_counter() - start

    def new_game(self):
        self.game += 1
        self.ai_seat = self.game % 2
        cfg = self.agent.settings
        self.arena = ProfessionArena(1, cfg['engine_threads'], cfg['root_width'])
        self.finished = False
        self.choose()
        return self.state()

    def state(self):
        return {**self.metadata, 'game':self.game, 'ai_seat':self.ai_seat, 'finished':self.finished,
                'round':int(self.arena.info[0,0]),
                'human':self.arena.observations[0,1-self.ai_seat].tolist(),
                'ai':self.arena.observations[0,self.ai_seat].tolist(),
                'legal':[self.catalog[int(i)] for i in np.flatnonzero(self.arena.masks[0,1-self.ai_seat])],
                'committed':self.committed is not None, 'think_seconds':self.think_seconds}

    def move(self, action):
        if self.finished or self.committed is None or not 0 <= action < 156 or not self.arena.masks[0,1-self.ai_seat,action]:
            raise ValueError('Illegal move or finished game')
        chosen_ai = self.committed
        turn = int(self.arena.info[0,0])
        choices = np.zeros((1,2),np.int32)
        choices[0,self.ai_seat], choices[0,1-self.ai_seat] = chosen_ai, action
        self.arena.step(choices)
        outcome = int(self.arena.results[0,0])
        self.finished = bool(outcome)
        self.committed = None
        reveal = {'game':self.game,'round':turn,'human_action':self.catalog[action],
                  'ai_action':self.catalog[chosen_ai],'outcome':outcome,'ai_seat':self.ai_seat,
                  'human_hp':float(self.arena.results[0,3-self.ai_seat]),
                  'ai_hp':float(self.arena.results[0,2+self.ai_seat])}
        if not self.finished:
            self.choose()
        return {'reveal':reveal, 'state':self.state() if not self.finished else None}
