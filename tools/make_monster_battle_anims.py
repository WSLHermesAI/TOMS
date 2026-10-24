#!/usr/bin/env python3
"""make_monster_battle_anims.py -- writes assets/media/anim/monster_battle.anim: how a monster moves on the battle screen,
one set of clips for every monster (docs/engine/05_ANIMATION.md "Monsters in battle"). The game plays them by the battle's
status (Game::updateMonsterRig):

    idle     looping: breathing, while nothing happens
    attack   the enemy's clock fires: a wind-up, a lunge at the player (left) at 0.2 s -- when its damage number
             appears (battle.json hitNumberDelayMs) -- and back
    hurt     the player's hit lands on it: knocked back, a red flash, a shake
    die      the fight is won: a last flash, it slumps and fades out, and stays gone

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

doc = {'version': 2, 'atlases': ['../atlas/game.atlas'], 'clips': [idle, attack, hurt, die]}
with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    f.write(json.dumps(doc, indent=2) + '\n')
print('wrote', os.path.relpath(OUT, ROOT), '-', ', '.join(c['name'] for c in doc['clips']))
