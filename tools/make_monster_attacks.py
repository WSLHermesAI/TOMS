"""tools/make_monster_attacks.py -- Each monster's attack beats (docs/gameplay/27 §2.4): enemies.json "attacks", one line per monster, written after its id.

The same pose always asks the same answer, so a player learns them once:
  lunge (it leans back, lunges across)      -> swipe right (the owner: "an attack from right to left: swipe right")
  sweep_low (it crouches, sweeps low)       -> swipe up (a hop)
  sweep_high (it rears, swings high)        -> swipe down (a duck)
  slam (it rises, drops)                    -> can't dodge: guard
  cast (it trembles and swells)             -> its magic in waves: a swipe each, the way shown
Small monsters (body 1): one attack, always the same (a tap on the beat, or a swipe with its way shown).
Stronger ones (body 2-3): several, at random, with no way shown: read the pose. Casters add magic waves.
Bosses: all of them, plus a slam that can't be dodged and a high swing that can't be guarded, taps and a drawn circle.
Re-running replaces the line."""
import json
import os
import re
import zlib
os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
P = 'assets/data/enemies.json'
CASTERS = {'wraith', 'lich', 'ghost_maiden', 'banshee', 'flame_spirit', 'fire_imp', 'dark_mage', 'frost_fairy', 'ice_wraith', 'ash_wraith',
           'pixie', 'stormlet', 'tomebat', 'demon', 'blizzard_owl', 'glow_worm'}

LUNGE = {'pose': 'lunge', 'answer': 'dodge', 'dodge': 'right'}
LOW = {'pose': 'sweep_low', 'answer': 'dodge', 'dodge': 'up'}
HIGH = {'pose': 'sweep_high', 'answer': 'dodge', 'dodge': 'down'}
SLAM = {'pose': 'slam', 'answer': 'guard', 'noDodge': True}
CAST = {'pose': 'cast', 'answer': 'move'}


def attacks(mid, m):
    body = m.get('body', 1) or 1
    boss = bool(m.get('bossTier')) or m.get('intro')
    if boss:
        return [dict(LUNGE, chance=25), dict(LOW, chance=15), dict(HIGH, noGuard=True, chance=15), dict(SLAM, chance=15),
                dict(CAST, waves=3, chance=15), {'pose': 'slam', 'answer': 'taps', 'need': 5, 'chance': 8},
                {'pose': 'cast', 'answer': 'shape', 'shape': 'circle', 'chance': 7}]
    if body <= 1:   # one fixed attack
        if zlib.crc32(mid.encode()) % 2:
            return [{'pose': 'lunge', 'answer': 'tap'}]
        return [dict(LUNGE, tell=True)]
    out = [dict(LUNGE, chance=45), dict(LOW, chance=30), dict(HIGH, chance=25)]
    if body >= 3:
        out += [dict(SLAM, chance=20), {'pose': 'slam', 'answer': 'taps', 'need': 4, 'chance': 10},
                {'pose': 'cast', 'answer': 'shape', 'shape': 'circle', 'chance': 8}]
    if mid in CASTERS:
        out.append(dict(CAST, waves=2 if body <= 2 else 3, chance=35))
    return out


data = json.load(open(P, encoding='utf-8'))
raw = open(P, 'rb').read().decode('utf-8')
crlf = '\r\n' in raw
s = raw.replace('\r\n', '\n')
s = re.sub(r'\n    "attacks": \[.*\],(?=\n)', '', s)   # re-run: the old line out
n = 0
for mid, m in data.items():
    if not isinstance(m, dict):
        continue
    head = '\n  "%s": {\n' % mid
    assert s.count(head) == 1, mid
    line = '    "attacks": ' + json.dumps(attacks(mid, m), ensure_ascii=False, separators=(', ', ': ')) + ',\n'
    s = s.replace(head, head + line)
    n += 1
if crlf:
    s = s.replace('\n', '\r\n')
open(P, 'wb').write(s.encode('utf-8'))
json.load(open(P, encoding='utf-8'))
print('attacks for', n, 'monsters')
