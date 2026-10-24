#!/usr/bin/env python3
"""make_archer_anim.py -- writes assets/media/anim/archer_bow.anim: a character's motion while shooting a bow, one set
of clips for every character (docs/engine/05_ANIMATION.md, "Characters"). Only the character: the bow is its own file
(weapon_bow.anim), and the game binds the two together (anim_player.h CharacterRig: the bow clip of the same name is
drawn at this clip's `hand` node, every frame).

    python tools/make_archer_anim.py

The clips are named and timed like the bow's (weapon_bow.anim): idle, charge, attack, miss. Each is

    archer
      lean                  the body's own motion (pivot at the feet): breathing, leaning back, recoil, flinch
        body_strain         (charge only) a looping shake while the shot is held
          body              the character: sprite "game:player", pivot at the bottom centre, feet at (0, 0)
        hand                no sprite: where a held thing is drawn -- its position, turn and scale (a 95-pixel bow at
                            a 32-pixel character's size). It moves with the lean, so the bow does too.

The names `body` and `hand` are the contract: a game or editor plays a clip with another character by giving `body`
another sprite and moving `hand` to the same place on that character (anim_clip.h fitCharacter): the hand keeps its
share of the visible body's height (head to feet) and its distance from the body's right edge, measured from the
sprite's pixels. So any 32 x 32 character (feet at the bottom of the image) plays these clips with no adjustment;
assets/data/character_fit.json can still nudge one character's hand.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_bow_anim as bow   # noqa: E402  (the bow's clip names, lengths and timing: the archer moves in step)

ROOT = bow.ROOT
OUT = os.path.join(ROOT, 'assets', 'media', 'anim', 'archer_bow.anim')

BODY_SPRITE = 'game:player'   # the character the clips are made with (any other is fitted at run time)
HAND = [9.0, -14.0, 0.0]      # the bow's grip, from the feet: in front of the body's right side, at hand height
HELD_SCALE = 0.32             # the hand's scale: a held thing made at bow size (95 px) drawn at a 32-pixel character's

key = bow.key
node = bow.node


def archer(clip, lean_tracks=None, strain=None, extra=None):
    """One archer clip, named and timed like the bow clip `clip` (none of the bow in it). strain: a looping shake
    around the body (charge)."""
    body = node('body', sprite=None, pivot=[0.5, 1.0])
    body['sprite'] = BODY_SPRITE
    body_tree = body
    if strain:
        body_tree = node('body_strain', loop=True, children=[body], tracks=strain)
    hand = node('hand', pos=HAND, scale=[HELD_SCALE, HELD_SCALE, 1.0], order=1)
    lean = node('lean', children=[body_tree, hand] + (extra or []))
    if lean_tracks:
        lean['tracks'] = lean_tracks
    out = {'name': clip['name'], 'length': clip['length'], 'root': node('archer', children=[lean])}
    for k in ('playCount', 'stayAtLastFrame'):
        if k in clip:
            out[k] = clip[k]
    return out


def idle():
    # Breathing in step with the bow's bob: the body rises a little (scale from the feet), the hand with it.
    breathe = {'scale': [key(0, [1, 1, 1], 'sinusoidalInOut'), key(0.8, [1, 1.03, 1], 'sinusoidalInOut'), key(1.6, [1, 1, 1])]}
    return archer(bow.idle(), breathe)


def charge():
    # Drawing: the body leans back from the feet (0.6 s); held: it shakes with the strain, looping like the bow's.
    lean = {'rot': [key(0, [0, 0, 0], 'quadraticOut'), key(0.6, [0, 0, -4])],
            'pos': [key(0, [0, 0, 0], 'quadraticOut'), key(0.6, [-1, 0, 0])]}
    strain = {'pos': [], 'rot': []}
    for i, (dx, dr) in enumerate([(0, 0), (0.6, 0.5), (-0.5, -0.4), (0.5, 0.6), (-0.6, -0.5), (0.4, 0.3), (-0.4, -0.6),
                                  (0.5, 0.4), (0, 0)]):
        t = 0.6 + 0.4 * i / 8
        strain['pos'].append(key(t, [dx, 0, 0]))
        strain['rot'].append(key(t, [0, 0, dr]))
    return archer(bow.charge(), lean, strain)


def attack():
    # From the leaned-back draw: the release kicks the body back a step, then it settles upright.
    lean = {'rot': [key(0, [0, 0, -4]), key(0.06, [0, 0, -4], 'quadraticOut'), key(0.14, [0, 0, 3], 'quadraticInOut'),
                    key(0.45, [0, 0, 0])],
            'pos': [key(0, [-1, 0, 0]), key(0.06, [-1, 0, 0], 'quadraticOut'), key(0.14, [-3, 0, 0], 'quadraticInOut'),
                    key(0.45, [0, 0, 0])],
            'scale': [key(0.06, [1, 1, 1], 'quadraticOut'), key(0.12, [1.04, 0.96, 1], 'quadraticInOut'), key(0.3, [1, 1, 1])]}
    return archer(bow.attack(), lean)


def miss():
    # The arrow slips: the body flinches forward, sags, and stays a little slumped.
    lean = {'rot': [key(0, [0, 0, -4]), key(0.08, [0, 0, -4], 'quadraticOut'), key(0.2, [0, 0, 6], 'sinusoidalInOut'),
                    key(0.55, [0, 0, 3], 'sinusoidalInOut'), key(1.0, [0, 0, 2])],
            'pos': [key(0, [-1, 0, 0]), key(0.08, [-1, 0, 0], 'quadraticOut'), key(0.2, [1, 0, 0], 'sinusoidalInOut'),
                    key(1.0, [0, 0, 0])],
            'scale': [key(0.08, [1, 1, 1], 'quadraticOut'), key(0.3, [1.03, 0.95, 1], 'sinusoidalInOut'), key(1.0, [1.02, 0.97, 1])]}
    return archer(bow.miss(), lean)


def main():
    data = {'version': 2, 'atlases': ['../atlas/game.atlas'], 'clips': [idle(), charge(), attack(), miss()]}
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=2) + '\n')
    print(f'{os.path.relpath(OUT, ROOT)}: clips {", ".join(c["name"] for c in data["clips"])}')


if __name__ == '__main__':
    main()
