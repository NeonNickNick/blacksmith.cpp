"""Exercise inference, legal play, commitment, replay, and export."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import numpy as np
import torch
from agent import Match
from arena import ProfessionArena, ROOT
from export_weights import export
from model import load
from search import search_tree
from training_core import Replay


class AgentTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        torch.set_num_threads(1)
        cls.model = load()

    def test_export_and_cpp_inference(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'build') as directory:
            output = Path(directory)/'policy.bin'
            export(ROOT/'checkpoint.pt',output)
            self.assertEqual(output.read_bytes(),(ROOT/'policy.bin').read_bytes())
            states = np.random.default_rng(912).normal(0,.3,(4,718)).astype(np.float32)
            binary = ROOT/'build'/('predict.exe' if os.name=='nt' else 'predict')
            with torch.no_grad():
                logits, values = self.model(torch.from_numpy(states))
            for i,state in enumerate(states):
                result=subprocess.run([str(binary),str(output)],input=' '.join(map(str,state)),
                                      text=True,capture_output=True,check=True)
                actual=np.fromstring(result.stdout,sep=' ')
                expected=np.r_[logits[i].numpy(),values[i].item()]
                np.testing.assert_allclose(actual,expected,atol=2e-5,rtol=2e-5)

    def test_replay_terminal_and_discount(self):
        replay=Replay(20)
        state=np.zeros((2,718),np.float32)
        mask=np.ones((2,156),np.float32)
        policy=mask/156
        replay.add_game([(state,mask,policy)]*2,1)
        np.testing.assert_allclose(replay.values[:4],[.997,-.997,1,-1])
        replay.add_game([(state,mask,policy)],3)
        np.testing.assert_array_equal(replay.values[4:6],[0,0])

    def test_search_legality_and_two_seats(self):
        arena=ProfessionArena(2,2,4)
        rng=np.random.default_rng(152)
        for _ in range(12):
            before=arena.observations.copy()
            actions,policies=search_tree(self.model,arena,rng,'cpu',leaf_width=2,iterations=8)
            np.testing.assert_array_equal(before,arena.observations)
            self.assertTrue((arena.masks[np.arange(2)[:,None],np.arange(2)[None,:],actions]>0).all())
            np.testing.assert_allclose(policies.sum(-1),1,atol=1e-5)
            self.assertTrue((policies[arena.masks==0]==0).all())
            arena.step(actions)

    def test_match_locks_and_restarts(self):
        match=Match()
        first=match.new_game()
        committed=match.committed
        with self.assertRaises(ValueError):
            match.move(-1)
        self.assertEqual(match.committed,committed)
        answer=match.move(first['legal'][0]['index'])
        self.assertEqual(answer['reveal']['ai_action']['index'],committed)
        second=match.new_game()
        self.assertEqual(second['round'],1)
        self.assertNotEqual(first['ai_seat'],second['ai_seat'])

    def test_console_input(self):
        result=subprocess.run([os.sys.executable,str(ROOT/'play.py')],
            input='invalid\n-9\niron\nnew\nq\n',text=True,encoding='utf-8',
            capture_output=True,env={**os.environ,'PYTHONUTF8':'1'},timeout=120)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('揭晓',result.stdout)
        self.assertIn('第 2 盘',result.stdout)


if __name__=='__main__':
    unittest.main(verbosity=2)
