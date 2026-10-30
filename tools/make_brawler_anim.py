#!/usr/bin/env python3
"""make_brawler_anim.py -- the brawler showcase: a painted HD character cut into parts and animated with .anim nodes only
(docs/engine/18_NODE_EFFECTS.md; the art: tools/art/make_brawler_art.py, its rig: tools/art/brawler_rig.json).

    python tools/make_brawler_anim.py
    anim_editor --headless meshes assets/media/anim/brawler.anim        (the legs' meshes, after a change of art)

    assets/media/anim/brawler.anim        idle, walk, run, punch, kick, hurt, victory, super_combo
    assets/media/anim/brawler_sword.anim  ready, raise, slash, fumble, attack_big   (+ weapon_sword.anim at `hand`)
    assets/media/anim/brawler_staff.anim  hold, focus, cast, fizzle, attack_big     (+ weapon_staff.anim)
    assets/media/anim/brawler_bow.anim    idle, charge, attack, miss, attack_big    (+ weapon_bow.anim)

What it shows the anim editor can do without Spine:
  * bones: one group node per joint, each part's sprite at its joint (its pivot); the clips turn the joints. The poses are
    written as targets -- where a foot or a fist goes -- and solved here with two-bone IK into joint turns, so the feet
    stay planted;
  * skinning: each arm is one mesh bent by its shoulder, elbow and wrist, each leg (trousers and boot) one bent by its
    hip, knee, ankle and toe (docs/engine/16), so elbows, knees and ankles fold smoothly;
  * lattice deform: the chest breathes, the sash's tail flutters;
  * sprite keys: the uppercut's flames are a flipbook of fx sprites;
  * effects (version 4, docs/engine/18): a node's `fx` draws its whole subtree into a picture of its own and through a
    shader -- motion blur on the striking limb, an HDR glow on the uppercut arm, a white flash on the whole body when hit,
    an outline aura for the super move -- and `trail` afterimages on the dash.

The character faces right. Weapons are never in these files: CharacterRig draws a weapon file's clip of the same name at
the `hand` node (docs/engine/05_ANIMATION.md "Characters"); `hand` is turned so the weapon keeps the angle each pose asks.
"""
import json
import math
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ANIM = os.path.join(ROOT, 'assets', 'media', 'anim')
RIG = json.load(open(os.path.join(ROOT, 'tools', 'art', 'brawler_rig.json'), encoding='utf-8'))
S = RIG['scale']
GX, GY = RIG['ground']
HELD_SCALE = 3.2          # a weapon made at ~90 px held by a ~565 px character

ORANGE = [1.0, 0.55, 0.15]
FIRE = [1.0, 0.62, 0.22]


def J(name):
    """A joint in clip pixels: the feet's centre at (0, 0), y down."""
    x, y = RIG['joints'][name]
    return ((x - GX) * S, (y - GY) * S)


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def rot(v, deg):
    r = math.radians(deg)
    c, s = math.cos(r), math.sin(r)
    return (v[0] * c - v[1] * s, v[0] * s + v[1] * c)


def ang(v):
    return math.degrees(math.atan2(v[1], v[0]))


def length(v):
    return math.hypot(v[0], v[1])


def r2(v):
    return round(v, 2)


# ---- the rig: rest offsets (each joint from its parent joint) -------------------------------------------------------

REST = {
    'body': J('pelvis'),
    'waist': sub(J('waist'), J('pelvis')),
    'neck': sub(J('neck'), J('waist')),
    'tie': sub(J('tie'), J('neck')),
    'sash': sub(J('sash_knot'), J('pelvis')),
}
for side in ('back', 'front'):
    p = side[0]
    REST[p + 's'] = sub(J(side + '_shoulder'), J('waist'))
    REST[p + 'e'] = sub(J(side + '_elbow'), J(side + '_shoulder'))
    REST[p + 'w'] = sub(J(side + '_wrist'), J(side + '_elbow'))
    REST[p + 'f'] = sub(J(side + '_fist'), J(side + '_wrist'))
    REST[p + 'h'] = sub(J(side + '_hip'), J('pelvis'))
    REST[p + 'k'] = sub(J(side + '_knee'), J(side + '_hip'))
    REST[p + 'a'] = sub(J(side + '_ankle'), J(side + '_knee'))
    REST[p + 't'] = sub(J(side + '_toe'), J(side + '_ankle'))
JOINTS = ['body', 'waist', 'neck', 'tie', 'sash', 'bs', 'be', 'bw', 'fs', 'fe', 'fw', 'bh', 'bk', 'ba', 'fh', 'fk', 'fa', 'hand']


def key(t, v, ease=None):
    k = {'t': round(t, 4), 'v': v}
    if ease and ease != 'linear':
        k['ease'] = ease
    return k


def node(name, sprite=None, **kw):
    n = {'name': name}
    if sprite:
        n['sprite'] = sprite
    for k, v in kw.items():
        if v is not None:
            n[k] = v
    return n


def part(name, **kw):
    pv = RIG['parts'][name]['pivot']
    return node(name, 'brawler:' + name, pivot=[pv[0], pv[1]], **kw)


# ---- poses: targets solved into joint turns --------------------------------------------------------------------------

def two_bone(base, a0, b0, target, bend):
    """Turns (degrees) of a two-bone chain from rest: base point, rest bone vectors a0 and b0 (in the chain's parent
    space), the target for the end. bend +1 / -1 picks the side the middle joint goes to (the sign of cross(target, mid))."""
    a, b = length(a0), length(b0)
    d = sub(target, base)
    dist = min(max(length(d), abs(a - b) + 1e-3), a + b - 1e-3)
    base_ang = math.atan2(d[1], d[0])
    alpha = math.acos(max(-1.0, min(1.0, (a * a + dist * dist - b * b) / (2 * a * dist))))
    best = None
    for s in (1, -1):
        th = base_ang + s * alpha
        mid = (a * math.cos(th), a * math.sin(th))
        cross = d[0] * mid[1] - d[1] * mid[0]
        if best is None or cross * bend > best[0]:
            best = (cross * bend, th, mid)
    _, th, mid = best
    end = (dist * math.cos(base_ang), dist * math.sin(base_ang))
    first = math.degrees(th) - ang(a0)
    second = ang(sub(end, mid)) - ang(b0) - first
    return first, second


MIN_REACH = 95   # the closest a wrist comes to its shoulder (the arm is ~137 px long)

GUARD = {
    'body': (0, 14), 'body_rot': 0, 'waist': 2, 'neck': -3,
    'feet': {'back': (-96, -37.5, 0), 'front': (98, -37.5, 0)},
    'hands': {'back': (-118, -440), 'front': (150, -285)},
    'fists': {'back': -95, 'front': -60},
}


def solve(pose):
    """A pose (targets, see GUARD) -> every joint's turn, and the body's position offset / scale."""
    p = {**GUARD, **pose}
    for k in ('feet', 'hands', 'fists'):   # one side given: the other keeps the guard's
        p[k] = {**GUARD[k], **pose.get(k, {})}
    out = {j: 0.0 for j in JOINTS}
    bx, by = p['body']
    body_rot = p.get('body_rot', 0.0)
    out['body'] = body_rot
    body_at = add(REST['body'], (bx, by))
    for j in ('waist', 'neck', 'tie', 'sash'):
        out[j] = p.get(j, 0.0)

    def to_body(v):   # clip space -> the body's space
        return rot(sub(v, body_at), -body_rot)

    # legs: the ankle at its target, the knee forward; the foot at its own angle (on screen)
    for side in ('back', 'front'):
        s = side[0]
        fx, fy, fa = p['feet'][side]
        h, k = two_bone(REST[s + 'h'], REST[s + 'k'], REST[s + 'a'], to_body((fx, fy)), 1)
        out[s + 'h'], out[s + 'k'] = h, k
        out[s + 'a'] = fa - body_rot - h - k
    # arms: the wrist at its target, the elbow down; the fist at its angle (on screen)
    waist_rot = out['waist']
    for side in ('back', 'front'):
        s = side[0]
        target = rot(sub(to_body(p['hands'][side]), REST['waist']), -waist_rot)   # in the waist's space
        shoulder = add(REST[s + 's'], p.get(s + 's_pos', (0, 0)))
        reach = sub(target, shoulder)   # never folded past ~100 degrees: the arm's mesh would crumple at the elbow
        if 0 < length(reach) < MIN_REACH:
            target = add(shoulder, (reach[0] * MIN_REACH / length(reach), reach[1] * MIN_REACH / length(reach)))
        out[s + 's'], out[s + 'e'] = two_bone(shoulder, REST[s + 'e'], REST[s + 'w'], target, p.get('elbow', {}).get(side, -1))
        chain = body_rot + waist_rot + out[s + 's'] + out[s + 'e']
        out[s + 'w'] = p['fists'][side] - chain - 90   # the fist's angle on screen: 0 points right; at rest it hangs down (90)
    out['hand'] = p.get('weapon', 0.0) - (body_rot + waist_rot + out['fs'] + out['fe'] + out['fw'])
    out['_body_pos'] = (bx, by)
    out['_body_scale'] = p.get('body_scale', (1, 1))
    out['_bs_pos'] = p.get('bs_pos', (0, 0))
    out['_fs_pos'] = p.get('fs_pos', (0, 0))
    return out


def world_of(pose, what):
    """Where a joint is in clip space for a pose (what: 'fwrist', 'bwrist', 'fankle', 'bankle', 'ffist', 'center')."""
    q = solve(pose)
    body_at = add(REST['body'], q['_body_pos'])
    br = q['body']
    if what == 'center':
        return add(body_at, rot(REST['waist'], br))
    if what in ('fankle', 'bankle'):
        s = what[0]
        p = add(body_at, rot(REST[s + 'h'], br))
        a1 = br + q[s + 'h']
        p = add(p, rot(REST[s + 'k'], a1))
        return add(p, rot(REST[s + 'a'], a1 + q[s + 'k']))
    s = what[0]
    a = br + q['waist']
    p = add(body_at, rot(REST['waist'], br))
    p = add(p, rot(add(REST[s + 's'], q['_' + s + 's_pos']), a))
    a += q[s + 's']
    p = add(p, rot(REST[s + 'e'], a))
    a += q[s + 'e']
    p = add(p, rot(REST[s + 'w'], a))
    if what.endswith('fist'):
        p = add(p, rot(REST[s + 'f'], a + q[s + 'w']))
    return p


class Timeline:
    """Keys per node and channel, from poses at times."""

    def __init__(self):
        self.tracks = {}
        self.last = {}

    def add(self, nodename, channel, t, v, ease=None):
        self.tracks.setdefault(nodename, {}).setdefault(channel, []).append(key(t, v, ease))

    def pose(self, t, pose, ease='sinusoidalInOut'):
        q = solve(pose)
        for j in JOINTS:
            a = q[j]
            prev = self.last.get(j)
            if prev is not None:   # the shorter way round from the previous key
                while a - prev > 180:
                    a -= 360
                while a - prev < -180:
                    a += 360
            self.last[j] = a
            self.add(NODE_OF[j], 'rot', t, [0, 0, r2(a)], ease)
        bx, by = q['_body_pos']
        self.add('body', 'pos', t, [r2(REST['body'][0] + bx), r2(REST['body'][1] + by), 0], ease)
        sx, sy = q['_body_scale']
        self.add('body', 'scale', t, [sx, sy, 1], ease)
        for s in ('b', 'f'):
            o = q['_' + s + 's_pos']
            self.add(NODE_OF[s + 's'], 'pos', t, [r2(REST[s + 's'][0] + o[0]), r2(REST[s + 's'][1] + o[1]), 0], ease)

    def get(self, nodename):
        return self.tracks.get(nodename, {})


NODE_OF = {'body': 'body', 'waist': 'waist', 'neck': 'neck', 'tie': 'tie', 'sash': 'sash_tail',
           'bs': 'back_shoulder', 'be': 'back_elbow', 'bw': 'back_wrist', 'fs': 'front_shoulder', 'fe': 'front_elbow',
           'fw': 'front_wrist', 'bh': 'back_hip', 'bk': 'back_knee', 'ba': 'back_ankle', 'fh': 'front_hip',
           'fk': 'front_knee', 'fa': 'front_ankle', 'hand': 'hand'}


# ---- the character's node tree -------------------------------------------------------------------------------------

BREATHE = {0: [-1, -3], 1: [0, -3.5], 2: [0, -3.5], 3: [1, -3], 4: [-2.5, -1.5], 7: [2.5, -1.5], 5: [0, -1], 6: [0, -1]}


def character(tl, extra=None, body_fx=None, body_trail=None, front_fx=None, back_fx=None, leg_fx=None, flames=None,
              arm_trail=None, under=None, forearm_fx=None):
    """The brawler's tree with the timeline's keys. extra: nodes over everything (impacts); under: nodes behind it (the
    dimmed screen, the shadow). *_fx / *_trail: (fx, fx keys) / (trail, trail keys) on the body, the arms, the front leg."""
    T = tl.get

    def with_fx(n, fx):
        if fx:
            n['fx'] = fx[0]
            if fx[1]:
                n.setdefault('tracks', {})['fx'] = fx[1]
        return n

    def joint_node(j, children, order=None, pos=None):
        tr = dict(T(NODE_OF[j]))
        p = pos if pos is not None else list(REST[j]) + [0]
        return node(NODE_OF[j], pos=[r2(p[0]), r2(p[1]), 0], order=order, children=children, tracks=tr or None)

    def leg(side, order, fx=None):
        # One mesh, trousers and boot, bent by the hip, knee and ankle (the toe: where the foot's bone points). Each
        # joint is a group; the mesh is a child of the hip, so an effect on the hip holds the whole leg.
        s = side[0]
        toe = node(side + '_toe', pos=[r2(REST[s + 't'][0]), r2(REST[s + 't'][1]), 0])
        ankle = joint_node(s + 'a', [toe])
        knee = joint_node(s + 'k', [ankle])
        bones = [side + '_hip', side + '_knee', side + '_ankle', side + '_toe']
        mesh = part(side + '_leg', skin={'bones': bones, 'weights': 'auto'}, meshGrid=[8, 16])
        return with_fx(joint_node(s + 'h', [knee, mesh], order=order), fx)

    def arm(side, order, fx=None, extra_hand=None, trail=None, inner_fx=None):
        # One mesh, upper arm and forearm, bent by the shoulder, elbow and wrist; the fist its own part over it (the
        # glove's band is the seam). The front arm: its blur on a group around the shoulder, so a glow on the shoulder
        # (the uppercut) is a layer inside the blur's.
        s = side[0]
        fist_children = [part(side + '_fist')]
        if side == 'front':
            fist_children.append(node('hand', pos=[r2(REST['ff'][0]), r2(REST['ff'][1]), 0],
                                      scale=[HELD_SCALE, HELD_SCALE, 1], order=1, tracks=dict(T('hand')) or None))
        fist_children += extra_hand or []
        wrist = joint_node(s + 'w', fist_children)
        elbow = joint_node(s + 'e', [wrist], order=1)
        mesh = part(side + '_arm', skin={'bones': [side + '_shoulder', side + '_elbow', side + '_wrist'], 'weights': 'auto'},
                    meshGrid=[12, 12])
        n = with_fx(joint_node(s + 's', [mesh, elbow]), inner_fx)
        if trail:
            n['trail'] = trail[0]
            if trail[1]:
                n.setdefault('tracks', {})['trail'] = trail[1]
        return with_fx(node(side + '_arm_fx', order=order, children=[n]), fx)

    torso = part('torso', lattice=[4, 4], tracks={'deform': BREATH_KEYS} if BREATH_KEYS else None)
    head = part('head')
    tie = joint_node('tie', [part('ponytail')], order=-1)
    neck = joint_node('neck', [tie, head], order=1)
    waist = joint_node('waist', [arm('back', -1, back_fx), torso, neck,
                                 arm('front', 2, front_fx, flames, arm_trail, forearm_fx)], order=2)
    cloth = part('sash_tail', lattice=[3, 4], tracks={'deform': SASH_KEYS} if SASH_KEYS else None)
    cloth['name'] = 'sash_cloth'
    sash = node('sash_tail', pos=[r2(REST['sash'][0]), r2(REST['sash'][1]), 0], order=1, children=[cloth],
                tracks=dict(T('sash_tail')) or None)
    body = node('body', pos=list(REST['body']) + [0],
                children=[leg('back', -4), leg('front', -3, leg_fx), part('pelvis'), sash, waist], tracks=dict(T('body')) or None)
    with_fx(body, body_fx)
    if body_trail:
        body['trail'] = body_trail[0]
        if body_trail[1]:
            body.setdefault('tracks', {})['trail'] = body_trail[1]
    shadow = node('shadow', 'fx:dot', pos=[0, -2, 0], scale=[7.5, 1.2, 1], color=[0, 0, 0, 0.35], order=-10)
    kids = (under or []) + [shadow, body] + (extra or [])
    return node('brawler', children=kids)


BREATH_KEYS = None
SASH_KEYS = None


def breath_keys(length, cycles=1):
    out = []
    for i in range(cycles * 2 + 1):
        t = length * i / (cycles * 2)
        d = BREATHE if i % 2 else {}
        out.append(key(t, {str(k): v for k, v in d.items()}, 'sinusoidalInOut'))
    return out


def sash_keys(length, cycles, amp):
    """The sash's tail flutters: its lower lattice rows swing side to side."""
    out = []
    n = cycles * 4
    for i in range(n + 1):
        t = length * i / n
        ph = math.sin(2 * math.pi * i / 4)
        d = {}
        for row in (2, 3):
            for col in range(3):
                d[str(row * 3 + col)] = [r2(amp * ph * row / 3), r2(-abs(ph) * amp * 0.3 * row / 3)]
        out.append(key(t, d, 'sinusoidalInOut'))
    return out


def clip(name, length, root, play_count=1, stay=True):
    c = {'name': name, 'length': length, 'root': root}
    if play_count != 1:
        c['playCount'] = play_count
    if not stay:
        c['stayAtLastFrame'] = False
    return c


def build(name, length, tl, play_count=1, breath=1, sash=(1, 4.0), **kw):
    global BREATH_KEYS, SASH_KEYS
    BREATH_KEYS = breath_keys(length, breath) if breath else None
    SASH_KEYS = sash_keys(length, sash[0], sash[1]) if sash else None
    return clip(name, length, character(tl, **kw), play_count)


def fx_keys(points):
    return [key(t, [r2(v) for v in vals], e) for t, vals, e in points]


def burst(name, at, t0, sprite='fx:ring', color=(1, 1, 1), size=(0.3, 2.4), life=0.25, order=20, alpha=1.0):
    """An additive flash that grows and fades at a point (impacts)."""
    c0 = list(color) + [0]
    c1 = list(color) + [alpha]
    return node(name, sprite, blend='add', pos=[r2(at[0]), r2(at[1]), 0], color=c0, scale=[size[0], size[0], 1], order=order,
                tracks={'color': [key(max(0, t0 - 0.01), c0), key(t0, c1, 'quadraticOut'), key(t0 + life, c0)],
                        'scale': [key(t0, [size[0], size[0], 1], 'quadraticOut'), key(t0 + life, [size[1], size[1], 1])]})


def hit_spark(name, at, t0, color=(1, 0.95, 0.8), big=1.0):
    return [burst(name + '_ring', at, t0, 'fx:ring', color, (0.4 * big, 2.6 * big), 0.22),
            burst(name + '_star', at, t0, 'fx:star', color, (0.6 * big, 2.0 * big), 0.14),
            burst(name + '_glow', at, t0, 'fx:glow', ORANGE, (0.8 * big, 2.2 * big), 0.2, alpha=0.8)]


def event_node(events):
    return node('events', tracks={'event': [key(t, v) for t, v in events]})


# ---- clips ---------------------------------------------------------------------------------------------------------

def idle():
    tl = Timeline()
    up = {'body': (0, 10), 'hands': {'back': (-118, -448), 'front': (150, -293)}, 'tie': 4, 'waist': 1}
    tl.pose(0, {})
    tl.pose(0.6, up)
    tl.pose(1.2, {})
    return build('idle', 1.2, tl, play_count=-1)


def walk():
    tl = Timeline()
    L = 0.9
    steps = 8
    for i in range(steps + 1):
        t = L * i / steps
        ph = 2 * math.pi * i / steps
        def foot(phase):
            # stance (phase 0..pi): on the ground, moving back; swing (pi..2pi): lifted, moving forward. Each foot steps
            # around its own place (the art faces the viewer: crossing legs would scissor)
            x = 30 * math.cos(phase)
            y = -37.5 - (28 * math.sin(phase - math.pi) if phase % (2 * math.pi) > math.pi else 0)
            return (x, y, 0 if y > -40 else -12 * math.sin(phase - math.pi))
        f = foot(ph % (2 * math.pi))
        b = foot((ph + math.pi) % (2 * math.pi))
        bob = 6 * abs(math.cos(ph))   # lowest when both feet are down
        swing = math.cos(ph)
        tl.pose(t, {'body': (8, 18 - 8 + bob - 6), 'body_rot': 3, 'waist': -2 * swing, 'neck': -2,
                    'feet': {'front': (f[0] + 78, f[1], f[2]), 'back': (b[0] - 72, b[1], b[2])},
                    'hands': {'front': (95 - 40 * swing, -280 + 6 * abs(swing)), 'back': (-70 + 40 * swing, -285)},
                    'fists': {'front': -10 + 30 * swing, 'back': 40 - 30 * swing},
                    'tie': 6 + 4 * math.sin(ph), 'sash': 4 * math.sin(ph)}, 'linear')
    return build('walk', L, tl, play_count=-1, breath=0, sash=(2, 5.0))


def run():
    tl = Timeline()
    L = 0.6
    steps = 8
    for i in range(steps + 1):
        t = L * i / steps
        ph = 2 * math.pi * i / steps
        def foot(phase):
            x = 48 * math.cos(phase)
            lifted = phase % (2 * math.pi) > math.pi
            y = -37.5 - (62 * math.sin(phase - math.pi) if lifted else 0)
            return (x, y, -25 * math.sin(phase - math.pi) if lifted else 0)
        f = foot(ph % (2 * math.pi))
        b = foot((ph + math.pi) % (2 * math.pi))
        bob = 12 * abs(math.sin(ph))
        swing = math.cos(ph)
        tl.pose(t, {'body': (24, 30 - bob), 'body_rot': 12, 'waist': -4 * swing, 'neck': -10,
                    'feet': {'front': (f[0] + 96, f[1], f[2]), 'back': (b[0] - 64, b[1], b[2])},
                    'hands': {'front': (120 - 70 * swing, -300 - 20 * max(0, swing)), 'back': (-40 + 70 * swing, -310)},
                    'fists': {'front': -40 + 50 * swing, 'back': -20 - 50 * swing},
                    'tie': -24 + 6 * math.sin(2 * ph), 'sash': -18 + 6 * math.sin(2 * ph)}, 'linear')
    trail = ({'count': 3, 'gap': 0.045, 'color': [0.75, 0.85, 1.0, 0.35]}, None)
    return build('run', L, tl, play_count=-1, breath=0, sash=(3, 7.0), body_trail=trail,
                 leg_fx=({'shader': 'motionBlur', 'params': [0, 0, 0.6, 0]}, None))


def punch():
    tl = Timeline()
    wind = {'body': (-6, 18), 'waist': 6, 'hands': {'front': (100, -330), 'back': (-50, -356)}, 'fists': {'front': -60, 'back': -60}}
    out = {'body': (26, 20), 'body_rot': 4, 'waist': -6, 'neck': -4,
           'hands': {'front': (300, -360), 'back': (-74, -340)}, 'fists': {'front': 0, 'back': -70}, 'tie': -10}
    tl.pose(0, {})
    tl.pose(0.1, wind, 'quadraticIn')
    tl.pose(0.18, out, 'quadraticOut')
    tl.pose(0.3, out)
    tl.pose(0.55, {})
    blur = ({'shader': 'motionBlur'}, fx_keys([(0.08, [0, 0, 0, 0], None), (0.12, [0, 0, 1.4, 0], None),
                                               (0.2, [0, 0, 1.4, 0], None), (0.26, [0, 0, 0, 0], None)]))
    at = world_of(out, 'ffist')
    return build('punch', 0.55, tl, front_fx=blur, extra=hit_spark('impact', (at[0] + 14, at[1]), 0.18) + [event_node([(0.18, 'hit')])])


def kick():
    tl = Timeline()
    chamber = {'body': (-10, 4), 'body_rot': -8, 'waist': 4, 'feet': {'front': (90, -170, -50), 'back': (-96, -37.5, 0)},
               'hands': {'front': (120, -340), 'back': (-120, -300)}, 'fists': {'front': -70, 'back': 30}, 'tie': 6}
    ext = {'body': (-22, -6), 'body_rot': -18, 'waist': 8, 'neck': 10,
           'feet': {'front': (230, -330, -70), 'back': (-96, -37.5, 0)},
           'hands': {'front': (100, -360), 'back': (-160, -330)}, 'fists': {'front': -80, 'back': 50}, 'tie': 14, 'sash': 10}
    tl.pose(0, {})
    tl.pose(0.16, chamber, 'quadraticIn')
    tl.pose(0.27, ext, 'quadraticOut')
    tl.pose(0.38, ext)
    tl.pose(0.52, chamber)
    tl.pose(0.75, {})
    blur = ({'shader': 'motionBlur'}, fx_keys([(0.12, [0, 0, 0, 0], None), (0.18, [0, 0, 1.3, 0], None),
                                               (0.27, [0, 0, 1.3, 0], None), (0.33, [0, 0, 0, 0], None)]))
    at = world_of(ext, 'fankle')
    return build('kick', 0.75, tl, leg_fx=blur, extra=hit_spark('impact', (at[0] + 30, at[1] - 10), 0.27, big=1.2) +
                 [event_node([(0.27, 'hit')])])


def hurt():
    tl = Timeline()
    hit = {'body': (-34, 26), 'body_rot': -12, 'waist': -8, 'neck': -18, 'tie': 20, 'sash': 12,
           'hands': {'front': (90, -270), 'back': (-120, -280)}, 'fists': {'front': 30, 'back': 60},
           'feet': {'back': (-110, -37.5, 0), 'front': (80, -37.5, 0)}}
    tl.pose(0, {})
    tl.pose(0.06, hit, 'quadraticOut')
    tl.pose(0.12, {**hit, 'body': (-30, 24)})
    tl.pose(0.5, {})
    flash = ({'shader': 'flash', 'color': [1, 1, 1, 1]}, fx_keys([(0, [0, 0, 0, 0], None), (0.04, [1, 0, 0, 0], None),
                                                                  (0.1, [1, 0, 0, 0], 'quadraticIn'), (0.3, [0, 0, 0, 0], None)]))
    return build('hurt', 0.5, tl, body_fx=flash)


def victory():
    tl = Timeline()
    pump = {'body': (0, 0), 'waist': -4, 'neck': -10, 'hands': {'front': (100, -560), 'back': (-60, -250)},
            'fists': {'front': -90, 'back': 80}, 'feet': {'back': (-70, -37.5, 0), 'front': (74, -37.5, 0)}, 'tie': 10}
    tl.pose(0, {})
    tl.pose(0.25, {**pump, 'body': (0, 30), 'hands': {'front': (120, -300), 'back': (-60, -250)}}, 'quadraticIn')
    tl.pose(0.45, pump, 'backOut')
    tl.pose(0.9, {**pump, 'tie': -6})
    tl.pose(1.6, {**pump, 'tie': 8})
    at = world_of(pump, 'ffist')
    glow = ({'shader': 'glow', 'color': [1.0, 0.85, 0.5, 1]},
            fx_keys([(0.4, [1, 10, 0.8, 0], None), (0.5, [1.8, 26, 0.6, 1.6], None), (1.0, [1.2, 18, 0.7, 0.8], None),
                     (1.6, [1.0, 12, 0.8, 0.3], None)]))
    return build('victory', 1.6, tl, front_fx=glow, sash=(2, 5.0),
                 extra=[burst('sparkle', (at[0], at[1] - 20), 0.45, 'fx:star', (1, 0.95, 0.7), (0.4, 1.8), 0.4)])


def super_combo():
    """A KOF-style super: the screen darkens and the brawler flares (outline aura), dashes in with afterimages, a rush of
    six blows (each limb blurred as it strikes), a flaming uppercut (HDR glow + flame flipbook) and a landing blast."""
    tl = Timeline()
    E = []          # events
    extra = []
    flare = {'body': (0, 40), 'waist': 4, 'neck': -6, 'feet': {'back': (-112, -37.5, 0), 'front': (110, -37.5, 0)},
             'hands': {'back': (-150, -300), 'front': (170, -300)}, 'fists': {'back': 120, 'front': 60}, 'tie': 18, 'sash': 14}
    tl.pose(0, {})
    tl.pose(0.12, flare, 'quadraticOut')
    tl.pose(0.45, {**flare, 'body': (0, 44)})
    E.append((0.12, 'super_flash'))
    # the dash (0.45 - 0.75): low and fast, the body moves 170 px
    dash = {'body': (150, 40), 'body_rot': 16, 'waist': -6, 'neck': -12,
            'feet': {'back': (40, -37.5, 0), 'front': (250, -37.5, 0)},
            'hands': {'back': (60, -300), 'front': (270, -330)}, 'fists': {'back': -20, 'front': -50}, 'tie': -26, 'sash': -16}
    tl.pose(0.75, dash, 'quadraticOut')
    # the rush (0.8 - 2.0): six blows, stepping forward
    base_x = 170
    blows = [
        ('front', 0.85, {'hands': {'front': (base_x + 300, -362), 'back': (base_x - 40, -350)}, 'fists': {'front': 0, 'back': -60}}),
        ('back', 1.05, {'bs_pos': (70, 0), 'waist': -10, 'hands': {'back': (base_x + 300, -350), 'front': (base_x + 110, -330)},
                        'fists': {'back': 0, 'front': -70}}),
        ('front', 1.25, {'hands': {'front': (base_x + 280, -420), 'back': (base_x - 40, -340)}, 'fists': {'front': -30, 'back': -60}}),
        ('leg', 1.45, {'body_rot': -16, 'feet': {'front': (base_x + 260, -300, -70), 'back': (base_x - 90, -37.5, 0)},
                       'hands': {'front': (base_x + 120, -360), 'back': (base_x - 150, -320)}, 'fists': {'front': -80, 'back': 50}}),
        ('back', 1.68, {'bs_pos': (70, 0), 'waist': -10, 'hands': {'back': (base_x + 290, -300), 'front': (base_x + 110, -330)},
                        'fists': {'back': 10, 'front': -70}}),
        ('front', 1.9, {'body_rot': 8, 'hands': {'front': (base_x + 320, -360), 'back': (base_x - 60, -330)},
                        'fists': {'front': 0, 'back': -60}}),
    ]
    stance = {'body': (base_x, 20), 'body_rot': 4, 'feet': {'back': (base_x - 96, -37.5, 0), 'front': (base_x + 110, -37.5, 0)},
              'hands': {'back': (base_x - 60, -350), 'front': (base_x + 130, -320)}, 'tie': -6}
    blur = {'front': [], 'back': [], 'leg': []}
    for i, (limb, t, strike) in enumerate(blows):
        step = 14 * i
        st = {**stance, 'body': (base_x + step, 20)}
        st['feet'] = {'back': (base_x - 96 + step, -37.5, 0), 'front': (base_x + 110 + step, -37.5, 0)}
        hit = {**st, **strike}
        if limb != 'leg':
            hit['feet'] = st['feet']
        hit['body'] = (base_x + step + 20, 18)
        tl.pose(t - 0.09, st, 'quadraticIn')
        tl.pose(t, hit, 'quadraticOut')
        blur[limb] += [(t - 0.07, [0, 0, 0, 0], None), (t - 0.03, [0, 0, 1.5, 0], None), (t + 0.02, [0, 0, 1.2, 0], None),
                       (t + 0.07, [0, 0, 0, 0], None)]
        at = world_of(hit, 'fankle' if limb == 'leg' else ('ffist' if limb == 'front' else 'bfist'))
        extra += hit_spark(f'hit{i}', (at[0] + 20, at[1]), t, big=0.9 + 0.1 * i)
        E.append((t, 'hit'))
    # the uppercut (2.05 - 2.6): crouch, then rise with the front arm up, flaming
    ux = base_x + 90
    crouch = {'body': (ux, 70), 'body_rot': 6, 'feet': {'back': (ux - 110, -37.5, 0), 'front': (ux + 100, -37.5, 0)},
              'hands': {'front': (ux + 60, -230), 'back': (ux - 80, -300)}, 'fists': {'front': 30, 'back': -40}, 'tie': 10}
    rise = {'body': (ux + 40, -150), 'body_rot': -10, 'neck': -14, 'waist': -6,
            'feet': {'back': (ux - 20, -110, 30), 'front': (ux + 90, -210, 10)},
            'hands': {'front': (ux + 140, -760), 'back': (ux - 60, -380)}, 'fists': {'front': -90, 'back': -40}, 'tie': 30, 'sash': 24}
    tl.pose(2.05, crouch, 'quadraticIn')
    tl.pose(2.3, rise, 'quadraticOut')
    tl.pose(2.55, {**rise, 'body': (ux + 46, -170)}, 'quadraticIn')
    for k, t in enumerate((2.18, 2.3, 2.42)):
        at = world_of(rise, 'ffist')
        extra += hit_spark(f'up{k}', (at[0], at[1] + 60 - 40 * k), t, color=(1, 0.8, 0.5), big=1.1)
        E.append((t, 'hit'))
    # the landing blast (2.6 - 3.0) and the hop back to where it started (3.0 - 3.6)
    land = {'body': (ux, 66), 'body_rot': 4, 'feet': {'back': (ux - 116, -37.5, 0), 'front': (ux + 110, -37.5, 0)},
            'hands': {'front': (ux + 170, -260), 'back': (ux - 150, -280)}, 'fists': {'front': 60, 'back': 120}, 'tie': -14,
            'body_scale': (1.04, 0.96)}
    tl.pose(2.8, land, 'quadraticOut')
    tl.pose(3.0, {**land, 'body': (ux, 40), 'body_scale': (1, 1)})
    hop = {'body': (ux / 2, -60), 'body_rot': -8, 'feet': {'back': (ux / 2 - 70, -90, 20), 'front': (ux / 2 + 60, -110, 10)},
           'hands': {'front': (ux / 2 + 120, -360), 'back': (ux / 2 - 60, -350)}, 'tie': 16}
    tl.pose(3.25, hop, 'quadraticOut')
    tl.pose(3.5, {}, 'quadraticIn')
    tl.pose(3.6, {})
    E.append((2.8, 'finish'))
    ground = (ux, -10)
    extra += [burst('blast_ring', ground, 2.8, 'fx:ring', (1, 0.7, 0.3), (0.6, 7.0), 0.4),
              burst('blast_glow', (ground[0], ground[1] - 60), 2.8, 'fx:glow', ORANGE, (1.0, 6.0), 0.45),
              burst('blast_smoke', (ground[0], ground[1] - 20), 2.82, 'fx:smoke', (0.6, 0.55, 0.5), (1.0, 4.0), 0.6, order=19,
                    alpha=0.6)]
    extra.append(event_node(E))
    # the screen darkens behind the brawler for the super flash and stays dim until the landing
    dim = node('dim', 'fx:dot', pos=[0, -300, 0], scale=[120, 120, 1], color=[0.02, 0.0, 0.05, 0], order=-20,
               tracks={'color': [key(0.05, [0.02, 0, 0.05, 0]), key(0.12, [0.02, 0, 0.05, 0.7], 'quadraticOut'),
                                 key(2.75, [0.02, 0, 0.05, 0.55]), key(3.1, [0.02, 0, 0.05, 0])]})
    flash_burst = burst('flare', (0, -300), 0.12, 'fx:glow', (1, 0.8, 0.5), (1.0, 9.0), 0.3)
    # effects: the whole body's outline aura; afterimages on the dash; each limb blurred as it strikes; the uppercut's
    # forearm glowing (HDR bloom) with a flame flipbook at the fist -- a layer inside the shoulder's motion-blur layer
    outline = ({'shader': 'outline', 'color': ORANGE + [1]},
               fx_keys([(0.1, [0, 2.5, 0, 0], None), (0.16, [9, 3.0, 0, 0], None), (0.3, [6, 2.2, 0, 0], None),
                        (0.45, [8, 2.6, 0, 0], None), (2.0, [5, 2.0, 0, 0], None), (2.3, [10, 3.5, 0, 0], None),
                        (2.9, [6, 2.0, 0, 0], None), (3.15, [0, 1.0, 0, 0], None)]))
    trail = ({'count': 5, 'gap': 0.035, 'color': ORANGE + [0.6], 'blend': 'add'},
             [key(0.4, 0.0), key(0.48, 1.0), key(0.78, 1.0), key(0.9, 0.0), key(2.2, 0.0), key(2.25, 0.8), key(2.6, 0.8),
              key(2.7, 0.0)])
    front_glow = ({'shader': 'glow', 'color': FIRE + [1]},
                  fx_keys([(2.05, [1, 0, 1, 0], None), (2.15, [1.6, 30, 0.55, 1.4], None), (2.3, [2.4, 44, 0.45, 2.4], None),
                           (2.55, [2.0, 36, 0.5, 1.8], None), (2.75, [1, 0, 1, 0], None)]))
    flames = node('flames', 'fx:flame_0', blend='add', pivot=[0.5, 0.9], pos=[0, -8, 0], scale=[2.6, 3.4, 1], rot=[0, 0, 180],
                  color=[1, 0.7, 0.3, 0], order=3,
                  tracks={'sprite': [key(2.12 + 0.05 * i, f'fx:flame_{i % 4}') for i in range(12)],
                          'color': [key(2.1, [1, 0.7, 0.3, 0]), key(2.16, [1, 0.7, 0.3, 1]), key(2.6, [1, 0.7, 0.3, 0.9]),
                                    key(2.72, [1, 0.7, 0.3, 0])]})
    root = character(tl_with_breath(tl, 3.6), extra=[flash_burst] + extra, under=[dim], body_fx=outline, body_trail=trail,
                     front_fx=({'shader': 'motionBlur'}, fx_keys(blur['front'])), forearm_fx=front_glow,
                     back_fx=({'shader': 'motionBlur'}, fx_keys(blur['back'])),
                     leg_fx=({'shader': 'motionBlur'}, fx_keys(blur['leg'])), flames=[flames])
    return clip('super_combo', 3.6, root)


def tl_with_breath(tl, length):
    global BREATH_KEYS, SASH_KEYS
    BREATH_KEYS = None
    SASH_KEYS = sash_keys(length, 6, 6.0)
    return tl


# ---- held weapons: the same character, poses for a weapon in the front hand --------------------------------------

def weapon_clips(kind):
    """Clips named and timed like weapon_<kind>.anim (the weapon is drawn at `hand`, turned to `weapon` degrees)."""
    if kind == 'sword':
        names = ('ready', 'raise', 'slash', 'fumble', 'attack_big')
        hold = {'hands': {'front': (120, -300), 'back': (-50, -330)}, 'fists': {'front': -80, 'back': -60}, 'weapon': 20}
        charged = {'body': (-8, 22), 'waist': 8, 'hands': {'front': (40, -420), 'back': (-40, -340)}, 'fists': {'front': -150},
                   'weapon': -10, 'tie': 8}
        strike = {'body': (30, 26), 'body_rot': 8, 'waist': -8, 'hands': {'front': (230, -300), 'back': (-80, -320)},
                  'fists': {'front': 20}, 'weapon': 0, 'tie': -12}
        times = {'charge': 0.5, 'hit': 0.18, 'big2': 0.38}
    elif kind == 'staff':
        names = ('hold', 'focus', 'cast', 'fizzle', 'attack_big')
        hold = {'hands': {'front': (110, -280), 'back': (-50, -330)}, 'fists': {'front': -90, 'back': -60}, 'weapon': 0}
        charged = {'body': (-4, 16), 'neck': -8, 'hands': {'front': (100, -340), 'back': (60, -380)}, 'weapon': -6, 'tie': 10}
        strike = {'body': (24, 22), 'body_rot': 6, 'hands': {'front': (220, -340), 'back': (-80, -330)}, 'fists': {'front': -30},
                  'weapon': 28, 'tie': -10}
        times = {'charge': 0.5, 'hit': 0.1, 'big2': 0.3}
    else:
        names = ('idle', 'charge', 'attack', 'miss', 'attack_big')
        hold = {'hands': {'front': (200, -360), 'back': (40, -360)}, 'fists': {'front': -90, 'back': 0}, 'weapon': 0, 'waist': -4}
        charged = {'body': (-10, 20), 'body_rot': -6, 'hands': {'front': (230, -370), 'back': (-30, -365)},
                   'fists': {'front': -90, 'back': 180}, 'weapon': -3, 'tie': 6}
        strike = {'body': (-16, 18), 'body_rot': -8, 'hands': {'front': (232, -372), 'back': (-80, -340)},
                  'fists': {'front': -90, 'back': 120}, 'weapon': 4, 'tie': -6}
        times = {'charge': 0.6, 'hit': 0.06, 'big2': 0.3}
    ready, charge, attack, miss, big = names
    clips = []
    tl = Timeline()
    tl.pose(0, hold)
    tl.pose(0.8 if kind != 'staff' else 0.9, {**hold, 'body': (0, 22), 'tie': 5})
    tl.pose(1.6 if kind != 'staff' else 1.8, hold)
    clips.append(build(ready, 1.6 if kind != 'staff' else 1.8, tl, play_count=-1))

    tl = Timeline()
    tl.pose(0, hold)
    tl.pose(times['charge'], charged, 'quadraticOut')
    for i, t in enumerate((0.6, 0.7, 0.8, 0.9, 1.0) if kind != 'bow' else (0.7, 0.8, 0.9, 1.0, 1.1, 1.2)):
        tl.pose(t, {**charged, 'body': (charged.get('body', (0, 14))[0] + (1.5 if i % 2 else -1.5), charged.get('body', (0, 14))[1])})
    L = 1.2 if kind == 'bow' else 1.0
    glow = ({'shader': 'glow', 'color': [0.6, 0.85, 1, 1]},
            fx_keys([(0, [1, 0, 1, 0], None), (times['charge'], [1.5, 20, 0.6, 1.2], None), (L, [1.5, 24, 0.55, 1.4], None)]))
    clips.append(build(charge, L, tl, front_fx=glow))

    hit = times['hit']
    L = {'sword': 0.6, 'staff': 0.7, 'bow': 0.7}[kind]
    tl = Timeline()
    tl.pose(0, charged)
    tl.pose(hit, strike, 'quadraticOut')
    tl.pose(hit + 0.15, strike)
    tl.pose(L, hold)
    blur = ({'shader': 'motionBlur'}, fx_keys([(max(0, hit - 0.06), [0, 0, 0, 0], None), (hit - 0.02, [0, 0, 1.3, 0], None),
                                               (hit + 0.06, [0, 0, 0, 0], None)]))
    clips.append(build(attack, L, tl, front_fx=blur))

    L = {'sword': 0.8, 'staff': 0.8, 'bow': 1.0}[kind]
    slump = {**hold, 'body': (-6, 34), 'body_rot': 5, 'neck': 14, 'hands': {'front': (100, -250), 'back': (-60, -280)},
             'fists': {'front': -40, 'back': 40}, 'weapon': 50, 'tie': 18}
    tl = Timeline()
    tl.pose(0, charged)
    tl.pose(0.25, {**strike, 'weapon': 30}, 'quadraticOut')
    tl.pose(L, slump)
    clips.append(build(miss, L, tl))

    tl = Timeline()
    tl.pose(0, charged)
    tl.pose(hit, {**strike, 'body': (strike['body'][0] + 14, strike['body'][1])}, 'quadraticOut')
    tl.pose(times['big2'], {**strike, 'body_rot': -4, 'hands': {'front': (200, -420), 'back': (-80, -320)}, 'weapon': -40}, 'quadraticOut')
    tl.pose(0.8, hold)
    aura = ({'shader': 'outline', 'color': [0.6, 0.85, 1, 1]},
            fx_keys([(0, [0, 1, 0, 0], None), (hit, [8, 3, 0, 0], None), (times['big2'], [6, 2.5, 0, 0], None), (0.8, [0, 1, 0, 0], None)]))
    trail = ({'count': 4, 'gap': 0.03, 'color': [0.6, 0.85, 1, 0.5], 'blend': 'add'},
             [key(0, 0.0), key(hit - 0.03, 1.0), key(times['big2'] + 0.05, 1.0), key(times['big2'] + 0.15, 0.0)])
    clips.append(build(big, 0.8, tl, body_fx=aura, body_trail=trail))
    return clips


def write(name, clips, atlases):
    data = {'version': 4, 'atlases': atlases, 'clips': clips}
    path = os.path.join(ANIM, name)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=1) + '\n')
    print(f'{os.path.relpath(path, ROOT)}: clips {", ".join(c["name"] for c in clips)}')


def main():
    atlases = ['../atlas/brawler.atlas', '../atlas/fx.atlas']
    write('brawler.anim', [idle(), walk(), run(), punch(), kick(), hurt(), victory(), super_combo()], atlases)
    write('brawler_sword.anim', weapon_clips('sword'), atlases)
    write('brawler_staff.anim', weapon_clips('staff'), atlases)
    write('brawler_bow.anim', weapon_clips('bow'), atlases)


if __name__ == '__main__':
    sys.exit(main())
