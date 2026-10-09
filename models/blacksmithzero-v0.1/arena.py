"""Public-state curriculum adapter. No action restrictions survive a reset."""
import ctypes
import pathlib
import os

ROOT = pathlib.Path(__file__).resolve().parent
LIBRARY = ROOT / 'build' / ('blacksmithzero.dll' if os.name == 'nt' else 'libblacksmithzero.so')
import numpy as np

ROLES = ['none', 'warlock', 'warlock_alchemy', 'cannon', 'driver', 'lancer']


class ProfessionArena:
    def __init__(self, count, threads, width):
        self.lib = ctypes.CDLL(str(LIBRARY))
        floats = np.ctypeslib.ndpointer(dtype=np.float32, flags='C_CONTIGUOUS')
        integers = np.ctypeslib.ndpointer(dtype=np.int32, flags='C_CONTIGUOUS')
        self.lib.zs_init.argtypes = [ctypes.c_int, ctypes.c_int]
        self.lib.zs_observe.argtypes = [floats, floats]
        self.lib.zs_step.argtypes = [integers, floats, floats, floats]
        self.lib.zp_reset_prefix.argtypes = [ctypes.c_int, ctypes.c_int, integers]
        self.lib.zp_metadata.argtypes = [integers]
        self.lib.zp_action.argtypes = [ctypes.c_int]
        assert self.lib.zs_init(count, threads) == 718
        assert self.lib.zs_actions() == 156
        self.count, self.width = count, width
        self.observations = np.zeros((count, 2, 718), np.float32)
        self.masks = np.zeros((count, 2, 156), np.float32)
        self.results = np.zeros((count, 4), np.float32)
        self.info = np.zeros((count, 3), np.int32)
        self.actions = [self.lib.zp_action(i) for i in range(8)]
        assert min(self.actions) >= 0
        self.refresh()

    def refresh(self):
        self.lib.zs_observe(self.observations, self.masks)
        self.lib.zp_metadata(self.info)

    def reset(self, env, prefix):
        moves = np.ascontiguousarray(prefix, dtype=np.int32).reshape(-1, 2)
        result = self.lib.zp_reset_prefix(env, len(moves), moves)
        if result:
            raise RuntimeError(f'Invalid curriculum prefix: {result}')
        self.results[env] = 0

    def step(self, actions):
        if self.lib.zs_step(np.ascontiguousarray(actions, dtype=np.int32), self.observations,
                            self.masks, self.results):
            raise RuntimeError('Illegal self-play action')
        self.lib.zp_metadata(self.info)
