#!/usr/bin/env python3
"""make_weapon_anims.py -- writes the sword's and the staff's battle clips (docs/30 phase 4; docs/15_ANIMATION.md
"Characters"): for each weapon the held thing and a character's motion, as two .anim files bound in code (anim_player.h
CharacterRig: the weapon clip of the same name is drawn at the character clip's `hand` node).

    python tools/make_weapon_anims.py

    assets/media/anim/weapon_sword.anim   + fighter_sword.anim   clips ready, raise, slash, fumble
    assets/media/anim/weapon_staff.anim   + caster_staff.anim    clips hold, focus, cast, fizzle

The clips have their own names: items.json "battleAnim": {"clips": {"idle": "ready", "charge": "raise", ...}} tells the
game which clip plays for which battle status (idle, charge, attack, miss). There is no weapon art yet, so the weapons
are built from the fx atlas's plain shapes, tinted (as the bow, tools/make_bow_anim.py). Like the bow's charge, the
charge clips hold a looping node: the raised sword trembles, the staff's orb glows, for as long as the attack waits.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_bow_anim as bow   # noqa: E402  (key / node helpers, the root folder)

key, node, R3 = bow.key, bow.node, bow.R3
ANIM = os.path.join(bow.ROOT, 'assets', 'media', 'anim')

STEEL = [0.80, 0.85, 0.92, 1]
EDGE = [0.96, 0.98, 1.0, 1]
GOLD = [0.92, 0.74, 0.28, 1]
LEATHER = [0.42, 0.25, 0.13, 1]
RUBY = [0.85, 0.20, 0.22, 1]
WOOD = [0.58, 0.38, 0.20, 1]
ORB = [0.40, 0.68, 1.0, 1]

BODY_SPRITE = 'game:player'
HELD_SCALE = 0.32             # a weapon made at ~90 px drawn at a 32-pixel character's size


def clip(name, length, root, play_count=1):
    c = {'name': name, 'length': length, 'root': root}
    if play_count != 1:
        c['playCount'] = play_count
    return c


def shake_keys(t0, t1, amp, rot_amp):
    """A looping tremble between t0 and t1 (pos and rot keys)."""
    jitter = [(0, 0, 0), (1.0, -0.6, 0.8), (-0.8, 0.7, -0.6), (0.7, 0.9, 0.9), (-1.0, -0.5, -0.8), (0.9, 0.4, 0.5),
              (-0.6, -0.9, -0.9), (0.8, 0.6, 0.6), (0, 0, 0)]
    pos, rot = [], []
    for i, (dx, dy, dr) in enumerate(jitter):
        t = t0 + (t1 - t0) * i / (len(jitter) - 1)
        pos.append(key(t, [round(dx * amp, 2), round(dy * amp, 2), 0]))
        rot.append(key(t, [0, 0, round(dr * rot_amp, 2)]))
    return {'pos': pos, 'rot': rot}


# ---- the sword: grip at the origin, the blade up (~80 px) -------------------------------------------------------------

def sword_parts(glow=None):
    parts = [
        node('blade', 'square', pivot=[0.5, 1.0], pos=[0.0, -9.0, 0.0], scale=[1.3, 8.4, 1.0], color=STEEL),
        node('edge', 'square', pivot=[0.5, 1.0], pos=[1.6, -9.0, 0.0], scale=[0.3, 8.0, 1.0], color=EDGE),
        node('tip', 'square', pos=[0.0, -76.0, 0.0], rot=[0, 0, 45], scale=[0.72, 0.72, 1.0], color=STEEL),
        node('guard', 'square', pos=[0.0, -8.0, 0.0], scale=[3.4, 0.8, 1.0], color=GOLD),
        node('grip', 'square', scale=[1.0, 1.9, 1.0], color=LEATHER),
        node('pommel', 'dot', pos=[0.0, 9.0, 0.0], scale=[0.3, 0.3, 1.0], color=RUBY),
    ]
    if glow:
        parts.insert(0, glow)
    return parts


def sword_clips():
    # ready: the blade sways a little, a glint runs up it now and then
    glint = node('glint', 'star', blend='add', pos=[0.0, -20.0, 0.0], color=[1, 1, 1, 0], scale=[0.05, 0.05, 1],
                 tracks={'pos': [key(1.0, [0, -20, 0], 'quadraticIn'), key(1.3, [0, -72, 0])],
                         'color': [key(1.0, [1, 1, 1, 0]), key(1.1, [1, 1, 1, 0.9]), key(1.3, [1, 1, 1, 0])],
                         'scale': [key(1.0, [0.05, 0.05, 1]), key(1.15, [0.25, 0.25, 1]), key(1.3, [0.05, 0.05, 1])]})
    ready = clip('ready', 1.6, node('sword', children=sword_parts() + [glint],
                                    tracks={'rot': [key(0, [0, 0, 4], 'sinusoidalInOut'), key(0.8, [0, 0, -2], 'sinusoidalInOut'),
                                                    key(1.6, [0, 0, 4])]}), -1)

    # raise: the sword goes back over the shoulder (0.5 s), then trembles there, glowing, for as long as it waits
    aura = node('aura', 'glow', blend='add', pos=[0.0, -42.0, 0.0], color=[0.6, 0.85, 1, 0.3], scale=[0.5, 1.6, 1], order=-1,
                tracks={'color': [key(0.5, [0.6, 0.85, 1, 0.3], 'sinusoidalInOut'), key(0.7, [0.6, 0.85, 1, 0.7], 'sinusoidalInOut'),
                                  key(0.9, [0.6, 0.85, 1, 0.3])]})
    shake = node('shake', loop=True, children=sword_parts(aura), tracks=shake_keys(0.5, 0.9, 1.2, 1.5))
    raise_ = clip('raise', 1.0, node('sword', children=[shake],
                                     tracks={'rot': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [0, 0, -70])],
                                             'pos': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [-4, -4, 0])],
                                             'event': [key(0.5, 'charged')]}))

    # slash: from over the shoulder, a fast arc forward with a trail, then back to ready
    trail = node('trail', 'spark', blend='add', pivot=[0.5, 1.0], pos=[0.0, -10.0, 0.0], color=[0.8, 0.9, 1, 0],
                 scale=[2.6, 8.0, 1], order=-1,
                 tracks={'color': [key(0.06, [0.8, 0.9, 1, 0]), key(0.1, [0.8, 0.9, 1, 0.8]), key(0.3, [0.8, 0.9, 1, 0])]})
    slash = clip('slash', 0.6, node('sword', children=sword_parts(trail),
                                    tracks={'rot': [key(0, [0, 0, -70], 'quadraticIn'), key(0.06, [0, 0, -78], 'quadraticOut'),
                                                    key(0.18, [0, 0, 85], 'quadraticOut'), key(0.3, [0, 0, 70], 'sinusoidalInOut'),
                                                    key(0.6, [0, 0, 0])],
                                            'pos': [key(0, [-4, -4, 0], 'quadraticOut'), key(0.18, [6, 2, 0], 'sinusoidalInOut'),
                                                    key(0.6, [0, 0, 0])],
                                            'event': [key(0.08, 'release'), key(0.18, 'hit')]}))

    # fumble: the swing comes out wrong, the sword wobbles and droops, a puff of dust
    puff = node('puff', 'smoke', pos=[0.0, -60.0, 0.0], color=[0.75, 0.72, 0.68, 0], scale=[0.3, 0.3, 1], order=2,
                tracks={'color': [key(0.2, [0.75, 0.72, 0.68, 0]), key(0.25, [0.75, 0.72, 0.68, 0.7]), key(0.7, [0.75, 0.72, 0.68, 0])],
                        'scale': [key(0.2, [0.3, 0.3, 1], 'quadraticOut'), key(0.7, [0.8, 0.8, 1])]})
    fumble = clip('fumble', 0.8, node('sword', children=sword_parts() + [puff],
                                      tracks={'rot': [key(0, [0, 0, -70], 'quadraticOut'), key(0.2, [0, 0, 30], 'sinusoidalInOut'),
                                                      key(0.35, [0, 0, 15], 'sinusoidalInOut'), key(0.5, [0, 0, 45], 'sinusoidalInOut'),
                                                      key(0.8, [0, 0, 60])],
                                              'event': [key(0.2, 'miss')]}))
    return [ready, raise_, slash, fumble]


# ---- the staff: grip at the origin, the orb on top (~90 px) -----------------------------------------------------------

def staff_parts(orb_extra=None, orb_scale=0.55):
    orb = [node('orb_glow', 'glow', blend='add', color=[0.45, 0.7, 1, 0.6], scale=[0.6, 0.6, 1]),
           node('orb', 'dot', color=ORB, scale=[orb_scale, orb_scale, 1]),
           node('orb_shine', 'dot', pos=[-2.0, -2.0, 0.0], color=[0.9, 0.97, 1, 1], scale=[0.15, 0.15, 1])]
    return [
        node('shaft', 'square', pivot=[0.5, 1.0], pos=[0.0, 14.0, 0.0], scale=[0.9, 11.0, 1.0], color=WOOD),
        node('prong_l', 'square', pivot=[0.5, 1.0], pos=[-3.0, -72.0, 0.0], rot=[0, 0, -25], scale=[0.4, 1.4, 1.0], color=GOLD),
        node('prong_r', 'square', pivot=[0.5, 1.0], pos=[3.0, -72.0, 0.0], rot=[0, 0, 25], scale=[0.4, 1.4, 1.0], color=GOLD),
        node('head', pos=[0.0, -82.0, 0.0], children=orb + (orb_extra or [])),
    ]


def staff_clips():
    # hold: a slow bob, the orb breathing
    hold = clip('hold', 1.8, node('staff', children=staff_parts(),
                                  tracks={'pos': [key(0, [0, 0, 0], 'sinusoidalInOut'), key(0.9, [0, -2, 0], 'sinusoidalInOut'),
                                                  key(1.8, [0, 0, 0])]}), -1)

    # focus: the staff rises a little and the orb gathers light: a pulsing glow, a ring, motes -- looping while it waits
    ring = node('ring', 'ring', blend='add', color=[0.6, 0.85, 1, 0.8], scale=[0.1, 0.1, 1],
                tracks={'scale': [key(0.5, [0.1, 0.1, 1], 'quadraticOut'), key(1.0, [0.8, 0.8, 1])],
                        'color': [key(0.5, [0.6, 0.85, 1, 0.8]), key(1.0, [0.6, 0.85, 1, 0])]})
    big = node('big_glow', 'glow', blend='add', color=[0.5, 0.75, 1, 0.5], scale=[0.9, 0.9, 1],
               tracks={'scale': [key(0.5, [0.9, 0.9, 1], 'sinusoidalInOut'), key(0.75, [1.3, 1.3, 1], 'sinusoidalInOut'),
                                 key(1.0, [0.9, 0.9, 1])]})
    motes = []
    for i, (x, y, d) in enumerate([(-18, 10, 0.0), (16, 14, 0.12), (-12, -14, 0.24), (14, -10, 0.36)]):
        motes.append(node(f'mote_{i}', 'star', blend='add', pos=[float(x), float(y), 0.0], color=[0.8, 0.9, 1, 0], scale=[0.12, 0.12, 1],
                          tracks={'pos': ([key(0.5, [x, y, 0])] if d else []) + [key(0.5 + d, [x, y, 0], 'quadraticIn'),
                                                                                  key(min(1.0, 0.64 + d), [0, 0, 0])],
                                  'color': ([key(0.5, [0.8, 0.9, 1, 0])] if d else []) + [key(0.5 + d, [0.8, 0.9, 1, 0.9]),
                                                                                         key(min(1.0, 0.64 + d), [0.8, 0.9, 1, 0])]}))
    glow = node('gather', loop=True, children=[big, ring] + motes, tracks={'rot': [key(0.5, [0, 0, 0]), key(1.0, [0, 0, 90])]})
    focus = clip('focus', 1.0, node('staff', children=staff_parts([glow], 0.62),
                                    tracks={'pos': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [0, -6, 0])],
                                            'rot': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [0, 0, -6])],
                                            'event': [key(0.5, 'charged')]}))

    # cast: the staff thrusts forward, the orb flashes, a bolt of light flies off
    flash = node('flash', 'ring', blend='add', color=[0.8, 0.95, 1, 0], scale=[0.2, 0.2, 1],
                 tracks={'color': [key(0.08, [0.8, 0.95, 1, 0]), key(0.1, [0.8, 0.95, 1, 1]), key(0.35, [0.8, 0.95, 1, 0])],
                         'scale': [key(0.08, [0.2, 0.2, 1], 'quadraticOut'), key(0.35, [1.2, 1.2, 1])]})
    bolt = node('bolt', 'glow', blend='add', color=[0.6, 0.85, 1, 0], scale=[0.6, 0.6, 1],
                tracks={'pos': [key(0.1, [0, 0, 0], 'quadraticIn'), key(0.45, [400, 30, 0])],
                        'color': [key(0.08, [0.6, 0.85, 1, 0]), key(0.1, [0.6, 0.85, 1, 1]), key(0.45, [0.6, 0.85, 1, 1]),
                                  key(0.47, [0.6, 0.85, 1, 0])]})
    cast = clip('cast', 0.7, node('staff', children=staff_parts([flash, bolt]),
                                  tracks={'rot': [key(0, [0, 0, -6], 'quadraticIn'), key(0.1, [0, 0, 28], 'quadraticOut'),
                                                  key(0.7, [0, 0, 0])],
                                          'pos': [key(0, [0, -6, 0], 'quadraticIn'), key(0.1, [6, 0, 0], 'quadraticOut'),
                                                  key(0.7, [0, 0, 0])],
                                          'event': [key(0.1, 'release'), key(0.45, 'hit')]}))

    # fizzle: the light sputters out, a puff of smoke, the staff sags
    sputter = node('sputter', 'glow', blend='add', color=[0.5, 0.7, 1, 0.8], scale=[0.8, 0.8, 1],
                   tracks={'color': [key(0, [0.5, 0.7, 1, 0.8]), key(0.08, [0.5, 0.7, 1, 0.2]), key(0.14, [0.5, 0.7, 1, 0.7]),
                                     key(0.22, [0.5, 0.7, 1, 0.1]), key(0.3, [0.5, 0.7, 1, 0.4]), key(0.45, [0.5, 0.7, 1, 0])]})
    smoke = node('smoke', 'smoke', color=[0.6, 0.6, 0.65, 0], scale=[0.3, 0.3, 1], order=2,
                 tracks={'color': [key(0.2, [0.6, 0.6, 0.65, 0]), key(0.3, [0.6, 0.6, 0.65, 0.7]), key(0.8, [0.6, 0.6, 0.65, 0])],
                         'pos': [key(0.2, [0, 0, 0], 'quadraticOut'), key(0.8, [4, -18, 0])],
                         'scale': [key(0.2, [0.3, 0.3, 1], 'quadraticOut'), key(0.8, [0.9, 0.9, 1])]})
    fizzle = clip('fizzle', 0.8, node('staff', children=staff_parts([sputter, smoke]),
                                      tracks={'rot': [key(0, [0, 0, -6], 'sinusoidalInOut'), key(0.3, [0, 0, 8], 'sinusoidalInOut'),
                                                      key(0.8, [0, 0, 14])],
                                              'pos': [key(0, [0, -6, 0], 'quadraticOut'), key(0.8, [0, 2, 0])],
                                              'event': [key(0.25, 'miss')]}))
    return [hold, focus, cast, fizzle]


# ---- the characters: body + hand, timed like their weapon's clips -----------------------------------------------------

def character(name, length, hand, lean_tracks=None, strain=None, play_count=1):
    body = node('body', pivot=[0.5, 1.0])
    body['sprite'] = BODY_SPRITE
    tree = node('body_strain', loop=True, children=[body], tracks=strain) if strain else body
    h = node('hand', pos=hand, scale=[HELD_SCALE, HELD_SCALE, 1.0], order=1)
    lean = node('lean', children=[tree, h])
    if lean_tracks:
        lean['tracks'] = lean_tracks
    return clip(name, length, node('fighter', children=[lean]), play_count)


def breathe(t):
    return {'scale': [key(0, [1, 1, 1], 'sinusoidalInOut'), key(t / 2, [1, 1.03, 1], 'sinusoidalInOut'), key(t, [1, 1, 1])]}


def body_strain(t0, t1):
    s = shake_keys(t0, t1, 0.5, 0.4)
    return {'pos': s['pos'], 'rot': s['rot']}


def fighter_clips():
    hand = [8.0, -13.0, 0.0]
    return [
        character('ready', 1.6, hand, breathe(1.6), play_count=-1),
        character('raise', 1.0, hand, {'rot': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [0, 0, -5])],
                                        'pos': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [-1, 0, 0])]}, body_strain(0.5, 0.9)),
        character('slash', 0.6, hand, {'rot': [key(0, [0, 0, -5]), key(0.06, [0, 0, -6], 'quadraticOut'), key(0.18, [0, 0, 7], 'sinusoidalInOut'),
                                               key(0.6, [0, 0, 0])],
                                       'pos': [key(0, [-1, 0, 0]), key(0.06, [-1, 0, 0], 'quadraticOut'), key(0.18, [4, 0, 0], 'sinusoidalInOut'),
                                               key(0.6, [0, 0, 0])]}),
        character('fumble', 0.8, hand, {'rot': [key(0, [0, 0, -5]), key(0.2, [0, 0, 8], 'sinusoidalInOut'), key(0.8, [0, 0, 4])],
                                        'scale': [key(0.2, [1, 1, 1], 'sinusoidalInOut'), key(0.5, [1.03, 0.95, 1], 'sinusoidalInOut'),
                                                  key(0.8, [1.02, 0.97, 1])]}),
    ]


def caster_clips():
    hand = [9.0, -12.0, 0.0]
    return [
        character('hold', 1.8, hand, breathe(1.8), play_count=-1),
        character('focus', 1.0, hand, {'pos': [key(0, [0, 0, 0], 'quadraticOut'), key(0.5, [0, -1, 0])]}, body_strain(0.5, 1.0)),
        character('cast', 0.7, hand, {'rot': [key(0, [0, 0, 0]), key(0.1, [0, 0, 5], 'quadraticOut'), key(0.7, [0, 0, 0])],
                                      'pos': [key(0, [0, -1, 0]), key(0.1, [2, 0, 0], 'quadraticOut'), key(0.7, [0, 0, 0])]}),
        character('fizzle', 0.8, hand, {'rot': [key(0, [0, 0, 0]), key(0.3, [0, 0, 6], 'sinusoidalInOut'), key(0.8, [0, 0, 4])],
                                        'scale': [key(0.3, [1, 1, 1], 'sinusoidalInOut'), key(0.8, [1.02, 0.96, 1])]}),
    ]


def write(name, clips, atlases):
    data = {'version': 2, 'atlases': atlases, 'clips': clips}
    path = os.path.join(ANIM, name)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=2) + '\n')
    print(f'{os.path.relpath(path, bow.ROOT)}: clips {", ".join(c["name"] for c in clips)}')


def main():
    write('weapon_sword.anim', sword_clips(), ['../atlas/fx.atlas'])
    write('fighter_sword.anim', fighter_clips(), ['../atlas/game.atlas'])
    write('weapon_staff.anim', staff_clips(), ['../atlas/fx.atlas'])
    write('caster_staff.anim', caster_clips(), ['../atlas/game.atlas'])


if __name__ == '__main__':
    main()
