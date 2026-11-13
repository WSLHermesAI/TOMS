#!/usr/bin/env python3
"""make_monster_battle_anims.py -- writes assets/media/anim/monster_battle.anim: how a monster moves on the battle screen,
one set of clips for every monster (docs/engine/05_ANIMATION.md "Monsters in battle"). The game plays them by the battle's
status (Game::updateMonsterRig):

    idle     looping: breathing, while nothing happens
    attack   the enemy's clock fires: a wind-up, a lunge at the player (left) at 0.2 s -- when its damage number
             appears (battle.json hitNumberDelayMs) -- and back
    hurt     the player's hit lands on it: knocked back, a red flash, a shake
    die      the fight is won: a last flash, it slumps and fades out, and stays gone
    special  a monster event's "anim": "special" (docs/gameplay/13 §4.6): it crouches, rears up glowing and shakes
    power    a special attack coming (its danger cells lit; battle.json "specialAttack", 3.5 s): it crouches, trembles harder
             and harder as it swells, pulsing red, then rears up in a roar and settles swollen
    charge   looping, after power until the hit: held swollen, quivering, glowing
    transform its HP-below change of form: it shakes, flashes white, swells and settles back

    python tools/make_monster_battle_anims.py

Each clip is
    monster
      motion      the moves (position, turn, scale), around the feet
        body      the monster: sprite "game:slime", pivot at the bottom centre; its colour flashes and fades
The `body` name is the contract (anim_clip.h fitCharacter): the game gives it the monster's own sprite, so every monster,
big ones included, plays these clips. A monster can have its own clip for a status: name it "<status>_<enemy id>" in this
file (for example "attack_golem"); the game plays it instead of the shared one.
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'media', 'anim', 'monster_battle.anim')
SPRITE = 'game:slime'   # the monster the clips are made with (any other is put in at run time)
EASE = 'sinusoidalInOut'


def key(t, v, ease=None):
    k = {'t': round(t, 3), 'v': v}
    if ease:
        k['ease'] = ease
    return k


def monster(name, length, motion=None, colour=None, events=None, play_count=1, stay=False):
    body = {'name': 'body', 'sprite': SPRITE, 'pivot': [0.5, 1.0]}
    if colour:
        body['tracks'] = {'color': colour}
    moving = {'name': 'motion', 'children': [body]}
    tracks = dict(motion or {})
    if events:
        tracks['event'] = events
    if tracks:
        moving['tracks'] = tracks
    clip = {'name': name, 'length': length, 'root': {'name': 'monster', 'children': [moving]}}
    if play_count != 1:
        clip['playCount'] = play_count
    if stay:
        clip['stayAtLastFrame'] = True
    return clip


WHITE = [1, 1, 1, 1]
RED = [1, 0.32, 0.32, 1]

idle = monster('idle', 1.4, play_count=-1, motion={
    'scale': [key(0, [1, 1, 1], EASE), key(0.7, [1.03, 0.97, 1], EASE), key(1.4, [1, 1, 1])],
    'pos': [key(0, [0, 0, 0], EASE), key(0.7, [0, -0.6, 0], EASE), key(1.4, [0, 0, 0])],
})

# The player stands on the left: the lunge goes to -x. The strike lands at 0.2 s.
attack = monster('attack', 0.55, motion={
    'pos': [key(0, [0, 0, 0], EASE), key(0.12, [4, 0.5, 0], 'quadraticIn'), key(0.2, [-14, -2, 0], 'quadraticOut'),
            key(0.32, [-10, 0, 0], EASE), key(0.55, [0, 0, 0])],
    'rot': [key(0, [0, 0, 0], EASE), key(0.12, [0, 0, 6], 'quadraticIn'), key(0.2, [0, 0, -10], 'quadraticOut'),
            key(0.55, [0, 0, 0])],
    'scale': [key(0, [1, 1, 1], EASE), key(0.12, [0.95, 1.06, 1], 'quadraticIn'), key(0.2, [1.12, 0.92, 1], 'quadraticOut'),
              key(0.36, [1, 1, 1])],
}, events=[key(0.2, 'hit')])

hurt = monster('hurt', 0.4, motion={
    'pos': [key(0, [0, 0, 0]), key(0.05, [5, 0, 0], 'quadraticOut'), key(0.1, [2, 0, 0]), key(0.15, [4, 0, 0]),
            key(0.24, [1, 0, 0], EASE), key(0.4, [0, 0, 0])],
    'rot': [key(0, [0, 0, 0]), key(0.05, [0, 0, 8], 'quadraticOut'), key(0.4, [0, 0, 0])],
    'scale': [key(0, [1, 1, 1]), key(0.05, [1.08, 0.9, 1], 'quadraticOut'), key(0.22, [1, 1, 1])],
}, colour=[key(0, WHITE), key(0.03, RED), key(0.2, [1, 0.55, 0.55, 1], EASE), key(0.4, WHITE)])

die = monster('die', 0.7, stay=True, motion={
    'pos': [key(0, [0, 0, 0]), key(0.1, [4, 0, 0], 'quadraticOut'), key(0.7, [8, 3, 0])],
    'rot': [key(0, [0, 0, 0]), key(0.1, [0, 0, 10], 'quadraticOut'), key(0.7, [0, 0, 24])],
    'scale': [key(0, [1, 1, 1]), key(0.1, [1.08, 0.92, 1]), key(0.7, [1.25, 0.35, 1], 'quadraticIn')],
}, colour=[key(0, RED), key(0.15, WHITE, EASE), key(0.7, [1, 1, 1, 0], 'quadraticIn')])

GLOW = [1, 0.75, 1, 1]
special = monster('special', 0.9, motion={
    'pos': [key(0, [0, 0, 0], EASE), key(0.2, [0, 2, 0], 'quadraticOut'), key(0.4, [0, -8, 0], 'quadraticOut'),
            key(0.62, [0, -6, 0], EASE), key(0.9, [0, 0, 0])],
    'scale': [key(0, [1, 1, 1], EASE), key(0.2, [1.12, 0.86, 1], 'quadraticOut'), key(0.4, [0.9, 1.18, 1], 'quadraticOut'),
              key(0.5, [1.06, 1.06, 1], EASE), key(0.9, [1, 1, 1])],
    'rot': [key(0.4, [0, 0, 0]), key(0.46, [0, 0, -4]), key(0.52, [0, 0, 4]), key(0.58, [0, 0, -3]), key(0.64, [0, 0, 0])],
}, colour=[key(0, WHITE), key(0.2, WHITE, EASE), key(0.4, GLOW, EASE), key(0.6, GLOW, EASE), key(0.9, WHITE)])

transform = monster('transform', 1.0, motion={
    'pos': [key(0, [0, 0, 0])] + [key(0.04 * (i + 1), [(-1) ** i * 2.5, 0, 0]) for i in range(10)] + [key(0.44, [0, 0, 0])],
    'scale': [key(0, [1, 1, 1]), key(0.44, [0.92, 0.92, 1], 'quadraticIn'), key(0.56, [1.35, 1.35, 1], 'quadraticOut'),
              key(0.8, [1.15, 1.15, 1], EASE), key(1.0, [1, 1, 1])],
}, colour=[key(0, WHITE), key(0.4, [1, 0.6, 0.6, 1], 'quadraticIn'), key(0.48, [1, 1, 1, 1], 'quadraticOut'),
           key(0.7, WHITE, EASE), key(1.0, WHITE)])

HOT = [1.55, 0.8, 0.6, 1]       # the power's red-orange pulse
FLARE = [2.0, 1.45, 1.1, 1]      # the roar's flash


def tremble(t0, t1, step, amp0, amp1, axis):
    """Shaking from t0 to t1, `step` s apart, growing from amp0 to amp1: [(t, value)] (axis: the value of one key)."""
    out, i, t = [], 0, t0
    while t < t1 - 1e-6:
        a = amp0 + (amp1 - amp0) * (t - t0) / max(1e-6, t1 - t0)
        out.append((t, axis((-1) ** i * a)))
        i += 1
        t += step
    return out


def power_keys(length=3.5):
    """The power's tracks (pos, rot, scale in the clip's units; colour), shared by both monster files."""
    pos = [(0, [0, 0, 0]), (0.35, [0, 1.5, 0])] + tremble(0.6, 2.9, 0.08, 0.5, 1.6, lambda a: [a, 0.6, 0]) +           [(3.05, [0, -5, 0]), (3.25, [0, -2, 0]), (length, [0, 0, 0])]
    rot = [(0, [0, 0, 0]), (0.35, [0, 0, 0])] + tremble(0.6, 2.9, 0.08, 1.0, 3.0, lambda a: [0, 0, a]) + [(3.05, [0, 0, 0])]
    scale = [(0, [1, 1, 1]), (0.35, [1.14, 0.84, 1]), (0.6, [1.04, 1.02, 1]), (2.9, [1.16, 1.14, 1]), (3.05, [0.94, 1.26, 1]),
             (3.25, [1.2, 1.18, 1]), (length, [1.12, 1.1, 1])]
    colour = [(0, WHITE)]
    t, k = 0.35, 0
    while t < 2.9:   # pulses, stronger each time
        s = (t - 0.35) / 2.55
        colour.append((t, [1 + (HOT[0] - 1) * (0.4 + 0.6 * s), 1 + (HOT[1] - 1) * (0.4 + 0.6 * s), 1 + (HOT[2] - 1) * (0.4 + 0.6 * s), 1] if k % 2 == 0 else WHITE))
        t += 0.35
        k += 1
    colour += [(3.05, FLARE), (3.25, HOT), (length, [1.35, 0.85, 0.7, 1])]
    return {'pos': pos, 'rot': rot, 'scale': scale}, colour


def charge_keys(length=0.5):
    pos = [(0, [0, 0, 0])] + tremble(0.06, length - 0.05, 0.06, 0.8, 0.8, lambda a: [a, 0, 0]) + [(length, [0, 0, 0])]
    scale = [(0, [1.12, 1.1, 1]), (length / 2, [1.15, 1.07, 1]), (length, [1.12, 1.1, 1])]
    colour = [(0, [1.35, 0.85, 0.7, 1]), (length / 2, [1.6, 0.95, 0.75, 1]), (length, [1.35, 0.85, 0.7, 1])]
    return {'pos': pos, 'scale': scale}, colour


_pm, _pc = power_keys()
power = monster('power', 3.5, motion={k: [key(t, v, EASE) for t, v in ks] for k, ks in _pm.items()},
                colour=[key(t, c, EASE) for t, c in _pc])
_cm, _cc = charge_keys()
charge = monster('charge', 0.5, play_count=-1, motion={k: [key(t, v, EASE) for t, v in ks] for k, ks in _cm.items()},
                 colour=[key(t, c, EASE) for t, c in _cc])

doc = {'version': 2, 'atlases': ['../atlas/game.atlas'], 'clips': [idle, attack, hurt, die, special, transform, power, charge]}
with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    f.write(json.dumps(doc, indent=2) + '\n')
print('wrote', os.path.relpath(OUT, ROOT), '-', ', '.join(c['name'] for c in doc['clips']))
