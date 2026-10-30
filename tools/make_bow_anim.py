#!/usr/bin/env python3
"""make_bow_anim.py -- writes assets/media/anim/weapon_bow.anim: a bow's four clips (docs/engine/05_ANIMATION.md, the .anim
format; docs/engine/06_ANIMATION_RECIPES.md for the parts).

    python tools/make_bow_anim.py

There is no bow art yet, so the bow is built from the fx atlas's plain shapes, tinted: a white `square` stretched into
limbs, grip, string, arrow shaft and fletching, `dot` tips, and `glow` / `ring` / `spark` / `smoke` / `star` effects.
The string is two pieces, each hung from a limb tip and turned / stretched to reach the arrow's nock; this script works
those angles out, so the string always meets the nock while the limbs bend.

Clips (the bow faces right, its grip at the clip's origin; ~95 px tall):
    idle     loops: a slow bob, a glint on the top tip
    charge   draws the arrow back (0.6 s), trembling near full draw, then holds: the whole bow shakes with the strain,
             a gold aura pulses along it, sparks rise off the limbs and the arrowhead glows -- all of it going on after
             the clip ends (a looping node), for as long as the shot is held. Event "charged" at full draw.
    attack   from full draw: the string snaps forward and rings, the arrow flies off with a trail, the bow recoils.
             Events "release" (the arrow leaves) and "hit" (it would reach a target ~400 px away).
    miss     from full draw: the arrow slips, tumbles short and drops, a puff of dust, the bow droops. Event "miss".
"""
import json
import math
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'media', 'anim', 'weapon_bow.anim')

# ---- the bow's shape --------------------------------------------------------------------------------------------
GRIP_HALF = 8.0        # the limbs start this far above / below the grip's centre
LIMB_LEN = 40.0        # each limb, px (the 8 px square scaled 5x)
REST_BEND = 16.0       # limb angle at rest, degrees (the tips lean back towards the archer)
DRAWN_BEND = 22.0      # at full draw
ARROW_LEN = 70.0
DRAWN_NOCK = -42.0     # the nock's x at full draw

WOOD = [0.55, 0.33, 0.16, 1]
GRIP = [0.30, 0.18, 0.09, 1]
GOLD = [1.0, 0.82, 0.30, 1]
STRING = [0.94, 0.92, 0.84, 1]
SHAFT = [0.80, 0.64, 0.42, 1]
STEEL = [0.78, 0.83, 0.90, 1]
FLETCH = [0.86, 0.20, 0.20, 1]

R3 = lambda v: [round(x, 3) for x in v]


def tips(bend):
    """The top and bottom limb tips at a bend angle (top limb turned -bend, bottom +bend)."""
    a = math.radians(bend)
    x = -LIMB_LEN * math.sin(a)
    y = GRIP_HALF + LIMB_LEN * math.cos(a)
    return (x, -y), (x, y)


def rest_nock():
    return tips(REST_BEND)[0][0]   # at rest the string is straight: the nock is level with the tips


def string_pose(bend, nock_x, nock_y=0.0):
    """Both string pieces for a limb bend and a nock point: (pos, rot z, length) of the top and bottom piece."""
    (tx, ty), (bx, by) = tips(bend)
    out = []
    # top piece: hangs down from the top tip (pivot top centre); a turn z sends its far end to x' = -L sin z
    dx, dy = nock_x - tx, nock_y - ty
    length = math.hypot(dx, dy)
    out.append(([tx, ty, 0], math.degrees(math.asin(-dx / length)), length))
    # bottom piece: reaches up from the bottom tip (pivot bottom centre); its far end goes to x' = L sin z
    dx, dy = nock_x - bx, nock_y - by
    length = math.hypot(dx, dy)
    out.append(([bx, by, 0], math.degrees(math.asin(dx / length)), length))
    return out


def key(t, v, ease=None):
    k = {'t': round(t, 3), 'v': v}
    if ease:
        k['ease'] = ease
    return k


def node(name, sprite=None, **kw):
    n = {'name': name}
    if sprite:
        n['sprite'] = 'fx:' + sprite
    for k, v in kw.items():
        if v is None:
            continue
        n[k] = R3(v) if isinstance(v, list) and v and isinstance(v[0], float) else v
    return n


def bow_parts(poses):
    """The limbs, tips, grip and string, keyed through `poses`: [(t, bend, nock_x, nock_y, ease), ...] (one pose
    = a still bow)."""
    def limb_keys(sign):
        return [key(t, [0, 0, sign * bend], e) for t, bend, _, _, e in poses]

    def tip_keys(i):
        return [key(t, R3([*tips(bend)[i], 0]), e) for t, bend, _, _, e in poses]

    def string_keys(i, what):
        ks = []
        for t, bend, nx, ny, e in poses:
            pos, rot, length = string_pose(bend, nx, ny)[i]
            v = {'pos': R3(pos), 'rot': [0, 0, round(rot, 2)], 'scale': [0.2, round(length / 8.0, 3), 1]}[what]
            ks.append(key(t, v, e))
        return ks

    first = poses[0]
    (tt, bt) = tips(first[1])
    sp = string_pose(first[1], first[2], first[3])
    many = len(poses) > 1
    parts = [
        node('limb_top', 'square', pivot=[0.5, 1.0], pos=[0.0, -GRIP_HALF, 0.0], scale=[0.9, 5.0, 1.0], color=WOOD,
             rot=[0, 0, -first[1]], tracks={'rot': limb_keys(-1)} if many else None),
        node('limb_bottom', 'square', pivot=[0.5, 0.0], pos=[0.0, GRIP_HALF, 0.0], scale=[0.9, 5.0, 1.0], color=WOOD,
             rot=[0, 0, first[1]], tracks={'rot': limb_keys(1)} if many else None),
        node('grip', 'square', scale=[1.4, 2.4, 1.0], color=GRIP),
        node('tip_top', 'dot', pos=[tt[0], tt[1], 0.0], scale=[0.2, 0.2, 1.0], color=GOLD,
             tracks={'pos': tip_keys(0)} if many else None),
        node('tip_bottom', 'dot', pos=[bt[0], bt[1], 0.0], scale=[0.2, 0.2, 1.0], color=GOLD,
             tracks={'pos': tip_keys(1)} if many else None),
        node('string_top', 'square', pivot=[0.5, 0.0], pos=sp[0][0], rot=[0, 0, round(sp[0][1], 2)],
             scale=[0.2, round(sp[0][2] / 8.0, 3), 1], color=STRING,
             tracks={'pos': string_keys(0, 'pos'), 'rot': string_keys(0, 'rot'), 'scale': string_keys(0, 'scale')} if many else None),
        node('string_bottom', 'square', pivot=[0.5, 1.0], pos=sp[1][0], rot=[0, 0, round(sp[1][1], 2)],
             scale=[0.2, round(sp[1][2] / 8.0, 3), 1], color=STRING,
             tracks={'pos': string_keys(1, 'pos'), 'rot': string_keys(1, 'rot'), 'scale': string_keys(1, 'scale')} if many else None),
    ]
    return parts


def arrow(pos, tracks=None, extra=None):
    """The arrow: a group whose origin is the nock, pointing right."""
    children = [
        node('shaft', 'square', pivot=[0.0, 0.5], scale=[ARROW_LEN / 8.0, 0.3, 1.0], color=SHAFT),
        node('head', 'square', pos=[ARROW_LEN, 0.0, 0.0], rot=[0, 0, 45], scale=[0.9, 0.9, 1.0], color=STEEL),
        node('fletch_top', 'square', pivot=[1.0, 0.5], pos=[9.0, 0.0, 0.0], rot=[0, 0, 28], scale=[1.4, 0.35, 1.0], color=FLETCH),
        node('fletch_bottom', 'square', pivot=[1.0, 0.5], pos=[9.0, 0.0, 0.0], rot=[0, 0, -28], scale=[1.4, 0.35, 1.0], color=FLETCH),
    ]
    a = node('arrow', pos=pos, order=1, children=children + (extra or []))
    if tracks:
        a['tracks'] = tracks
    return a


def clip(name, length, play_count, root_children, root_tracks=None, stay=True):
    root = node('bow', children=root_children)
    if root_tracks:
        root['tracks'] = root_tracks
    c = {'name': name, 'length': length, 'root': root}
    if play_count != 1:
        c['playCount'] = play_count
    if not stay:
        c['stayAtLastFrame'] = False
    return c


REST_X = rest_nock()
REST = (REST_BEND, REST_X, 0.0)
DRAWN = (DRAWN_BEND, DRAWN_NOCK, 0.0)


def idle():
    glint = node('glint', 'star', blend='add', pos=R3([*tips(REST_BEND)[0], 0]), color=[1, 0.95, 0.7, 0], scale=[0.05, 0.05, 1],
                 tracks={'color': [key(0, [1, 0.95, 0.7, 0]), key(1.0, [1, 0.95, 0.7, 0], 'quadraticOut'),
                                   key(1.12, [1, 0.95, 0.7, 0.9], 'quadraticIn'), key(1.3, [1, 0.95, 0.7, 0])],
                         'scale': [key(1.0, [0.05, 0.05, 1], 'quadraticOut'), key(1.12, [0.3, 0.3, 1], 'quadraticIn'),
                                   key(1.3, [0.05, 0.05, 1])],
                         'rot': [key(1.0, [0, 0, 0]), key(1.3, [0, 0, 90])]})
    parts = bow_parts([(0, *REST, None)]) + [arrow([REST_X, 0.0, 0.0]), glint]
    bob = {'pos': [key(0, [0, 0, 0], 'sinusoidalInOut'), key(0.8, [0, -2.5, 0], 'sinusoidalInOut'), key(1.6, [0, 0, 0])],
           'rot': [key(0, [0, 0, 0], 'sinusoidalInOut'), key(0.8, [0, 0, -2], 'sinusoidalInOut'), key(1.6, [0, 0, 0])]}
    return clip('idle', 1.6, -1, parts, bob)


def charge():
    """Draws the bow (0.6 s), then holds it, the whole bow shaking with the strain and glowing, for as long as the shot
    is held. A looping node loops every key under it, so the bow is built twice: `drawing` (0 - 0.6 s, drawn back, then
    hidden) and `held` (shown from 0.6 s: the same full-draw pose inside `shake`, a looping group whose keys all lie in
    0.6 - 1.0 s, so it shakes on after the clip ends instead of drawing again)."""
    # The draw: the strain shows as a tremble that starts near full draw.
    draw_shake = {'pos': [key(0, [0, 0, 0]), key(0.35, [0, 0, 0])], 'visible': [key(0, True), key(0.6, False)]}
    for i, (dx, dy) in enumerate([(0.4, -0.3), (-0.5, 0.4), (0.7, 0.3), (-0.8, -0.5), (0.9, 0.6)]):
        draw_shake['pos'].append(key(0.38 + 0.045 * i, [dx, dy, 0]))
    draw_shake['pos'].append(key(0.6, [0, 0, 0]))
    gather = node('gather', 'glow', blend='add', pos=[ARROW_LEN, 0.0, 0.0], color=[1, 0.8, 0.3, 0], scale=[0.5, 0.5, 1],
                  tracks={'color': [key(0, [1, 0.8, 0.3, 0], 'quadraticIn'), key(0.6, [1, 0.8, 0.3, 1])]})
    drawing = node('drawing', children=bow_parts([(0, *REST, 'quadraticOut'), (0.6, *DRAWN, None)]) +
                   [arrow([REST_X, 0.0, 0.0], {'pos': [key(0, [REST_X, 0, 0], 'quadraticOut'), key(0.6, [DRAWN_NOCK, 0, 0])]}, [gather])],
                   tracks=draw_shake)

    # The hold: everything keyed in 0.6 - 1.0 s, looping.
    T0, T1 = 0.6, 1.0
    STRAIN = 1.5   # how hard the held bow shakes (1 = up to 1.6 px, 1.4 degrees)
    jitter = [(dx * STRAIN, dy * STRAIN, dr * STRAIN) for dx, dy, dr in
              [(0, 0, 0), (1.6, -1.0, 1.2), (-1.3, 1.2, -0.9), (1.1, 1.4, 1.4), (-1.6, -0.8, -1.2), (1.4, 0.6, 0.8),
               (-0.9, -1.4, -1.4), (1.2, 1.0, 1.0), (0, 0, 0)]]
    shake_tracks = {'pos': [], 'rot': []}
    for i, (dx, dy, dr) in enumerate(jitter):
        t = T0 + (T1 - T0) * i / (len(jitter) - 1)
        shake_tracks['pos'].append(key(t, [round(dx, 2), round(dy, 2), 0]))
        shake_tracks['rot'].append(key(t, [0, 0, round(dr, 2)]))
    tremble = [key(T0, [DRAWN_NOCK, 0, 0])]
    for i, dy in enumerate([0.8, -0.7, 0.6, -0.8, 0.7, -0.6, 0.5, 0.0]):
        tremble.append(key(T0 + (T1 - T0) * (i + 1) / 8, [DRAWN_NOCK, dy, 0]))
    pulse = node('pulse', 'glow', blend='add', color=[1, 0.8, 0.3, 1], scale=[0.5, 0.5, 1],
                 tracks={'scale': [key(T0, [0.5, 0.5, 1], 'sinusoidalInOut'), key(0.8, [0.85, 0.85, 1], 'sinusoidalInOut'),
                                   key(T1, [0.5, 0.5, 1])]})
    ring = node('spark_ring', 'ring', blend='add', color=[1, 0.9, 0.5, 0.8], scale=[0.1, 0.1, 1],
                tracks={'scale': [key(T0, [0.1, 0.1, 1], 'quadraticOut'), key(T1, [0.6, 0.6, 1])],
                        'color': [key(T0, [1, 0.9, 0.5, 0.8]), key(T1, [1, 0.9, 0.5, 0])]})
    power = node('power', pos=[ARROW_LEN, 0.0, 0.0], children=[pulse, ring])
    # The bow's own power: a tall gold aura along it, pulsing, and sparks rising off both limbs.
    aura = node('aura', 'glow', blend='add', pos=[-6.0, 0.0, 0.0], color=[1, 0.75, 0.25, 0.25], scale=[0.55, 1.7, 1], order=-1,
                tracks={'color': [key(T0, [1, 0.75, 0.25, 0.25], 'sinusoidalInOut'), key(0.8, [1, 0.75, 0.25, 0.6], 'sinusoidalInOut'),
                                  key(T1, [1, 0.75, 0.25, 0.25])],
                        'scale': [key(T0, [0.55, 1.7, 1], 'sinusoidalInOut'), key(0.8, [0.65, 1.85, 1], 'sinusoidalInOut'),
                                  key(T1, [0.55, 1.7, 1])]})
    motes = []
    for i, (x, y, rise, delay) in enumerate([(-10, -34, 18, 0.0), (-12, 30, 16, 0.13), (-6, -18, 14, 0.26), (-9, 40, 20, 0.07)]):
        t0 = T0 + delay
        t1 = min(T1, t0 + 0.28)
        motes.append(node(f'mote_{i}', 'star', blend='add', pos=[float(x), float(y), 0.0], color=[1, 0.9, 0.5, 0], scale=[0.12, 0.12, 1],
                          tracks={'pos': ([key(T0, [x, y, 0])] if delay else []) + [key(t0, [x, y, 0], 'quadraticOut'), key(t1, [x - 3, y - rise, 0])],
                                  'color': ([key(T0, [1, 0.9, 0.5, 0])] if delay else []) + [key(t0, [1, 0.9, 0.5, 0.9], 'quadraticIn'),
                                            key(t1, [1, 0.9, 0.5, 0])],
                                  'rot': [key(T0, [0, 0, 0]), key(T1, [0, 0, 180])]}))
    shake = node('shake', loop=True, children=[aura] + bow_parts([(0, *DRAWN, None)]) +
                 [arrow([DRAWN_NOCK, 0.0, 0.0], {'pos': tremble}, [power])] + motes, tracks=shake_tracks)
    # Hidden until full draw: both keys, as before a track's first key it holds that key's value (a lone "shown at
    # 0.6 s" key would show it from the start, a second bow and string over the drawing one).
    held = node('held', visible=False, children=[shake], tracks={'visible': [key(0, False), key(T0, True)]})

    lean = {'rot': [key(0, [0, 0, 0], 'quadraticOut'), key(0.6, [0, 0, -3])],
            'event': [key(0.6, 'charged')]}
    return clip('charge', 1.2, 1, [drawing, held], lean)


def attack():
    # Release at 0.06: the string snaps past straight and rings out; the limbs spring back with a small overshoot.
    poses = [(0, *DRAWN, 'quadraticIn'), (0.06, 14.0, REST_X + 7.0, 0.0, 'quadraticOut'), (0.12, 17.0, REST_X - 4.0, 0.0, 'quadraticOut'),
             (0.18, 15.5, REST_X + 2.0, 0.0, 'quadraticOut'), (0.26, *REST, None)]
    fly = {'pos': [key(0, [DRAWN_NOCK, 0, 0], 'quadraticIn'), key(0.06, [REST_X, 0, 0]), key(0.45, [420, -6, 0])],
           'color': [key(0.3, [1, 1, 1, 1]), key(0.45, [1, 1, 1, 0])],
           'visible': [key(0, True), key(0.46, False)]}
    trail = node('trail', 'spark', blend='add', pivot=[1.0, 0.5], color=[1, 0.85, 0.5, 0], scale=[1.5, 0.6, 1],
                 tracks={'color': [key(0.05, [1, 0.85, 0.5, 0]), key(0.08, [1, 0.85, 0.5, 0.9]), key(0.4, [1, 0.85, 0.5, 0.2])],
                         'scale': [key(0.06, [0.5, 0.6, 1], 'quadraticOut'), key(0.2, [2.2, 0.6, 1])]})
    flash = node('flash', 'ring', blend='add', pos=[REST_X, 0.0, 0.0], color=[1, 0.95, 0.75, 0], scale=[0.15, 0.15, 1], order=2,
                 tracks={'color': [key(0.05, [1, 0.95, 0.75, 0]), key(0.07, [1, 0.95, 0.75, 1], 'quadraticOut'),
                                   key(0.3, [1, 0.95, 0.75, 0])],
                         'scale': [key(0.06, [0.15, 0.15, 1], 'quadraticOut'), key(0.3, [0.9, 0.9, 1])]})
    parts = bow_parts(poses) + [arrow([DRAWN_NOCK, 0.0, 0.0], fly, [trail]), flash]
    recoil = {'pos': [key(0.06, [0, 0, 0], 'quadraticOut'), key(0.12, [-6, 0, 0], 'quadraticInOut'), key(0.4, [0, 0, 0])],
              'rot': [key(0, [0, 0, -3]), key(0.06, [0, 0, -3], 'quadraticOut'), key(0.12, [0, 0, 4], 'quadraticInOut'),
                      key(0.4, [0, 0, 0])],
              'event': [key(0.06, 'release'), key(0.42, 'hit')]}
    return clip('attack', 0.7, 1, parts, recoil)


def miss():
    # The fingers slip at 0.08: a weak release, the string flaps longer, the arrow tumbles short and drops.
    poses = [(0, *DRAWN, 'quadraticIn'), (0.08, 15.0, REST_X + 10.0, 0.0, 'quadraticOut'), (0.16, 18.0, REST_X - 8.0, 0.0, 'quadraticOut'),
             (0.24, 15.0, REST_X + 6.0, 0.0, 'quadraticOut'), (0.32, 17.0, REST_X - 4.0, 0.0, 'quadraticOut'),
             (0.42, 15.5, REST_X + 2.0, 0.0, 'quadraticOut'), (0.55, *REST, None)]
    tumble = {'pos': [key(0, [DRAWN_NOCK, 0, 0], 'quadraticOut'), key(0.08, [REST_X, 0, 0], 'quadraticOut'),
                      key(0.3, [55, -14, 0], 'quadraticIn'), key(0.75, [95, 70, 0])],
              'rot': [key(0.08, [0, 0, 0], 'quadraticIn'), key(0.3, [0, 0, 40], 'linear'), key(0.75, [0, 0, 200])],
              'color': [key(0.6, [1, 1, 1, 1]), key(0.85, [1, 1, 1, 0])],
              'visible': [key(0, True), key(0.86, False)]}
    puff = node('puff', 'smoke', pos=[REST_X + 6.0, 0.0, 0.0], color=[0.75, 0.72, 0.68, 0], scale=[0.25, 0.25, 1], order=2,
                tracks={'color': [key(0.07, [0.75, 0.72, 0.68, 0]), key(0.1, [0.75, 0.72, 0.68, 0.7], 'quadraticOut'),
                                  key(0.6, [0.75, 0.72, 0.68, 0])],
                        'scale': [key(0.08, [0.25, 0.25, 1], 'quadraticOut'), key(0.6, [0.7, 0.7, 1])],
                        'pos': [key(0.08, [REST_X + 6.0, 0, 0], 'quadraticOut'), key(0.6, [REST_X + 10.0, -14, 0])]})
    parts = bow_parts(poses) + [arrow([DRAWN_NOCK, 0.0, 0.0], tumble), puff]
    droop = {'pos': [key(0.08, [0, 0, 0], 'quadraticOut'), key(0.35, [0, 4, 0], 'sinusoidalInOut'), key(1.0, [0, 3, 0])],
             'rot': [key(0, [0, 0, -3]), key(0.08, [0, 0, -3], 'quadraticOut'), key(0.35, [0, 0, 14], 'sinusoidalInOut'),
                     key(1.0, [0, 0, 9])],
             'event': [key(0.08, 'miss')]}
    return clip('miss', 1.0, 1, parts, droop)


def attack_big():
    """A perfect result (weapon_play.json "results" anim, docs/gameplay/13 §4.2): the release of attack(), with a volley
    of three arrows, a bigger gold flash and a burst of light off the bow. Same timing: release 0.06, hit 0.42."""
    c = attack()
    c['name'] = 'attack_big'
    parts = c['root']['children']
    for i, dy in enumerate((-9.0, 9.0)):
        fly = {'pos': [key(0, [DRAWN_NOCK, dy * 0.3, 0], 'quadraticIn'), key(0.06 + 0.02 * (i + 1), [REST_X, dy, 0]),
                       key(0.47, [420, dy * 3 - 6, 0])],
               'color': [key(0, [1, 1, 1, 0]), key(0.06, [1, 1, 1, 0]), key(0.09, [1, 0.95, 0.75, 1]), key(0.32, [1, 0.95, 0.75, 1]),
                         key(0.47, [1, 0.95, 0.75, 0])],
               'visible': [key(0, True), key(0.48, False)]}
        trail = node('trail', 'spark', blend='add', pivot=[1.0, 0.5], color=[1, 0.8, 0.35, 0], scale=[1.5, 0.6, 1],
                     tracks={'color': [key(0.07, [1, 0.8, 0.35, 0]), key(0.1, [1, 0.8, 0.35, 0.9]), key(0.42, [1, 0.8, 0.35, 0.2])],
                             'scale': [key(0.08, [0.5, 0.6, 1], 'quadraticOut'), key(0.22, [2.4, 0.6, 1])]})
        parts.append(arrow([DRAWN_NOCK, 0.0, 0.0], fly, [trail]))
        parts[-1]['name'] = f'arrow_{i + 2}'
    burst = node('burst', 'glow', blend='add', pos=[REST_X, 0.0, 0.0], color=[1, 0.8, 0.3, 0], scale=[0.4, 0.4, 1], order=-1,
                 tracks={'color': [key(0.04, [1, 0.8, 0.3, 0]), key(0.08, [1, 0.8, 0.3, 0.9], 'quadraticOut'), key(0.5, [1, 0.8, 0.3, 0])],
                         'scale': [key(0.06, [0.4, 0.4, 1], 'quadraticOut'), key(0.5, [1.8, 1.8, 1])]})
    ring = node('ring_big', 'ring', blend='add', pos=[REST_X, 0.0, 0.0], color=[1, 0.9, 0.6, 0], scale=[0.2, 0.2, 1], order=2,
                tracks={'color': [key(0.08, [1, 0.9, 0.6, 0]), key(0.1, [1, 0.9, 0.6, 1]), key(0.45, [1, 0.9, 0.6, 0])],
                        'scale': [key(0.08, [0.2, 0.2, 1], 'quadraticOut'), key(0.45, [1.6, 1.6, 1])]})
    parts += [burst, ring]
    c['root']['tracks']['pos'] = [key(0.06, [0, 0, 0], 'quadraticOut'), key(0.12, [-10, 0, 0], 'quadraticInOut'), key(0.5, [0, 0, 0])]
    c['length'] = 0.8
    return c


def main():
    data = {'version': 2, 'atlases': ['../atlas/fx.atlas'], 'clips': [idle(), charge(), attack(), miss(), attack_big()]}
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=2) + '\n')
    print(f'{os.path.relpath(OUT, ROOT)}: clips {", ".join(c["name"] for c in data["clips"])}')


if __name__ == '__main__':
    main()
