import json
from pathlib import Path

import blacksmith.core as bs

script_dir = Path(__file__).resolve().parent
file_path = script_dir / 'data.txt'
pl = json.loads(file_path.read_text(encoding='utf-8'))
param = bs.BlacksmithZeroParam()
param.from_list(pl)
param.operate_mode()
platform = bs.StandardPVP(True)
zero = bs.BlacksmithZero()
zero.param = param
while(True):
    while(True):
        cmd = input()
        ctx = platform.to_context(cmd, platform.player())
        if ctx is not None:
            platform.submit_player_context(ctx)
            break
    platform.submit_enemy_context(zero.choose_enemy_skill(platform.player(), platform.enemy(), platform.round()))

