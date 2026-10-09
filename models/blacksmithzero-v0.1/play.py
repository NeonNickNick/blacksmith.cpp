"""Persistent local console for BlacksmithZero v0.1."""
import ctypes
import json
import os
import time
from pathlib import Path
from agent import Match

_match = None

NAMES = ['打铁','棍击','钻击','砍击','撕裂','盾','荆棘盾','恢复','转空间','转时间','反射','延迟保护','12点护甲','转职术士','炼金','转职炮手','转职时空','转职战矛','鲜血印记','转魔力','法术攻击','法术盾','献祭','沉默','点金','单炮','双炮','三炮','穿甲弹','炮盾','空间攻击','时间转空间','空间转时间','空间屏障','天击','霸碎','龙牙','三连刺','蓄力','升龙','血刃','嗜血','鲜血恢复','血怒']


def request(path, data=None):
    global _match
    if _match is None:
        print('正在加载 BlacksmithZero v0.1……', flush=True)
        _match = Match()
        _match.new_game()
    if path == '/state':
        return _match.state()
    if path == '/new':
        return _match.new_game()
    if path == '/move':
        return _match.move(int(data['action']))
    raise ValueError('Unknown local operation')


def description(action):
    return f"{NAMES[action['skill_id']]} {action['parameter']}"


def show(label, values):
    ordinary = round(values[9] * 10, 2)
    gold = round(values[4] * 10 - ordinary, 2)
    print(f'{label}  血量 {values[2]*10:g}/{values[3]*10:g}  普通铁 {ordinary:g} / 金铁 {gold:g}'
          f'  魔力 {values[8]*10:g}  空间 {values[6]*10:g}  时间 {values[7]*10:g}')
    for kind, name in enumerate(['物理免疫', '法术免疫', '百分比减伤', '真实减伤', '荆棘减伤', '普通减伤', '石壳', '真实护甲', '普通护甲']):
        for delay in range(6):
            power = values[214 + kind * 6 + delay] * 10
            if power > .001:
                print(f'    {name} {power:g} / 延迟 {delay}')
    for kind, name in enumerate(['物理', '法术', '真实']):
        for delay in range(6):
            power = values[160 + (kind * 6 + delay) * 3] * 10
            if power > .001:
                print(f'    待结算{name}攻击 {power:g} / 延迟 {delay}')


def play():
    print('=== 你 vs BlacksmithZero v0.1 ===')
    print('AI 先锁定动作，双方同时揭晓；每盘交换位置。')
    print('输入当前菜单编号，带参数如 2 3；也可输入英文技能名。')
    print('new 重开，q 退出。\n')
    current = request('/state')
    print('模型校验：' + current['checkpoint_sha256'][:12])
    if current['finished']:
        current = request('/new', {})
    while True:
        print(f"\n========== 第 {current['game']} 盘 / 回合 {current['round']} ==========")
        show('你', current['human'])
        show('AI', current['ai'])
        groups = {}
        for action in current['legal']:
            groups.setdefault(action['skill_id'], []).append(action)
        menu = list(groups.values())
        for number, actions in enumerate(menu, 1):
            first = actions[0]
            parameters = [item['parameter'] for item in actions]
            suffix = '  N=' + ','.join(map(str, parameters)) if len(parameters)>1 or parameters[0] else ''
            print(f"{number:2}. {NAMES[first['skill_id']]} [{first['skill'].lower()}]{suffix}")
        print(f"AI 已锁定（{current['device']} 思考 {current['think_seconds']:.2f} 秒）。")
        while True:
            tokens = input('你的出招 > ').strip().lower().split()
            if not tokens:
                continue
            if tokens[0] in ('q', 'quit'):
                return
            if tokens[0] == 'new':
                current = request('/new', {})
                break
            actions = next((items for i,items in enumerate(menu,1)
                            if tokens[0] in (str(i),items[0]['skill'].lower(),NAMES[items[0]['skill_id']])),None)
            if actions is None or len(tokens)>2:
                print('请选择当前菜单中的技能。')
                continue
            try:
                parameter = int(tokens[1]) if len(tokens)==2 else (
                    actions[0]['parameter'] if len(actions)==1 else int(input('参数 N > ')))
            except ValueError:
                print('参数请输入整数。')
                continue
            chosen = next((item for item in actions if item['parameter']==parameter),None)
            if chosen is None:
                print('这个参数当前不合法。')
                continue
            answer = request('/move', {'action':chosen['index']})
            reveal = answer['reveal']
            print(f"揭晓：你 {description(reveal['human_action'])} | AI {description(reveal['ai_action'])}")
            print(f"结算后血量：你 {reveal['human_hp']:g} / AI {reveal['ai_hp']:g}")
            if reveal['outcome']:
                outcome = reveal['outcome']
                print('平局。' if outcome==3 else '达到100回合上限。' if outcome==4 else
                      'AI 获胜。' if outcome-1==reveal['ai_seat'] else '你赢了！')
                if input('回车再来一盘，q 退出 > ').strip().lower()=='q':
                    return
                current = request('/new', {})
            else:
                current = answer['state']
            break


if __name__ == '__main__':
    if os.name == 'nt':
        ctypes.windll.kernel32.SetConsoleTitleW('BlacksmithZero v0.1')
    try:
        play()
    except (EOFError, KeyboardInterrupt):
        pass
    except Exception as error:
        print(f'对战结束：{error}')
        raise SystemExit(1)
