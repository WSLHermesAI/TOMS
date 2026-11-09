#!/usr/bin/env python3
"""make_battle_fx.py -- adds the battle's result and status effects to assets/media/fx/battle.particle (docs/gameplay/13
§4.2 "what has to be made first", step 4 of §7). The other effects in the file (the weapons' release / hit tiers, the
combo hints) are kept as they are; the ones made here are replaced each run, by name.

    python tools/make_battle_fx.py

    <weapon>_perfect   every point matched (weapon_play.json "results" fx): the weapon's top hit, bigger, with a
                       shock ring and a shower of stars
    <weapon>_great     most points matched: the weapon's middle hit with a ring
    miss_puff          a fail or 0 points: a grey puff at the player
    monster_special    data/looks.json monster "special": a rising glow and motes
    monster_transform  "transform": a white flash, two rings, sparks thrown out
    monster_parried    "parry": blue-white sparks off the parried blow
    monster_broken     "broken": yellow stars bursting out, a dizzy ring

The special moves' effects (docs/gameplay/16 §6, data/special_moves.json):

    star_trail         a sparkle left along the pentagram as it is drawn (many, one after another: a trail)
    star_burst         a corner of the drawn pentagram: a star flash, a ring, sparks
    star_burst_big     a corner the player reached: the same, bigger, with a shower of stars
    seal_trail         a monster's seal drawn (red-violet sparkles)
    seal_burst         a corner of its seal
    shield_crack       the player's guard breaking: grey shards and a ring

The attack feel (docs/gameplay/20, data/battle.json "feelFx"):

    wand_hint_1..4     the staff's counted hints, each tier bigger than the last (the game also scales them up one by one)
    wand_vanish        all the hints gone at once: a pop at each
    wand_return        a circle coming back
    combo_spray_1..5   a hit's spray on the monster, tier by the combo
    sword_slash        streaks along the cut (the game turns it to the swipe), growing and shrinking, three afterimages
    bow_gather_1..3    particles drawn in to the arrow while the bow is pulled, tier by the charge
    bow_explode        the arrow bursting where it hits
    monster_claw       a monster's plain strike landing on the hero: three claw streaks and sparks
    monster_cast(_magic)  a big monster's wind-up / a spell's casting, at the monster
    monster_bolt(_big), monster_orb   what it throws: a head and a trail (the game flies it, then stops it)
    monster_blast(_magic), monster_smash   where it lands

<weapon> is bow, sword, staff and default (a weapon without its own fx, unarmed). Open the file in particle_editor to
look at them or tune them.
"""
import copy
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'media', 'fx', 'battle.particle')

WEAPON_TINT = {'bow': [1, 0.85, 0.45, 1], 'sword': [0.8, 0.92, 1, 1], 'staff': [0.65, 0.8, 1, 1], 'default': [1, 0.95, 0.8, 1]}


def burst(count, t=0, repeat=None, interval=None):
    b = {'t': t, 'count': count}
    if repeat:
        b['repeat'], b['interval'] = repeat, interval
    return {'rate': 0, 'bursts': [b]}


FADE = [{'t': 0, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0], 'ease': 'quadraticIn'}]


def ring(name, colour, size, grow, life=0.4, delay=0, count=1, repeat=None, interval=None, order=0):
    return {'name': name, 'maxParticles': (repeat or 0) + count, 'emission': burst(count, delay, repeat, interval),
            'life': life, 'speed': 0, 'size': size, 'color': colour, 'sprite': 'fx:ring',
            'overLife': {'color': FADE, 'size': [{'t': 0, 'v': 1, 'ease': 'cubicOut'}, {'t': 1, 'v': grow}]},
            'render': {'blend': 'add', 'order': order}}


def flash(name, sprite, colour, size, life=0.18, order=2):
    return {'name': name, 'maxParticles': 1, 'emission': burst(1), 'life': life, 'speed': 0, 'size': size,
            'rotation': [0, 45], 'color': colour, 'sprite': sprite,
            'overLife': {'color': FADE, 'size': [{'t': 0, 'v': 0.6, 'ease': 'backOut'}, {'t': 1, 'v': 1.6}]},
            'render': {'blend': 'add', 'order': order}}


def sparks(name, sprite, colours, count, speed, size, life=(0.3, 0.6), gravity=120, outward=True, direction=None,
           spread=None, spin=False, order=1):
    e = {'name': name, 'maxParticles': count[1] if isinstance(count, list) else count, 'emission': burst(count),
         'shape': {'type': 'point', 'outward': outward} if outward else {'type': 'point'},
         'life': list(life), 'speed': list(speed), 'size': list(size), 'color': colours, 'sprite': sprite,
         'overLife': {'color': [{'t': 0.5, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0]}],
                      'size': [{'t': 0, 'v': 1}, {'t': 1, 'v': 0.4}]},
         'forces': {'drag': 3, 'gravity': [0, gravity]},
         'render': {'blend': 'add', 'alignToVelocity': not spin, 'order': order}}
    if direction is not None:
        e['direction'] = direction
    if spread is not None:
        e['spread'] = spread
    if spin:
        e['rotation'] = [0, 360]
        e['spin'] = [-360, 360]
    return e


def scaled(effect, name, k):
    """A copy of `effect`, its particles k times as many and as big."""
    e = copy.deepcopy(effect)
    e['name'] = name
    for m in e['emitters']:
        m['maxParticles'] = max(1, round(m['maxParticles'] * k))
        for b in m['emission'].get('bursts', []):
            b['count'] = [max(1, round(c * k)) for c in b['count']] if isinstance(b['count'], list) else max(1, round(b['count'] * k))
        if isinstance(m.get('size'), list):
            m['size'] = [round(s * k, 1) for s in m['size']]
        elif isinstance(m.get('size'), (int, float)):
            m['size'] = round(m['size'] * k, 1)
    return e


def result_effects(by_name):
    out = []
    for w, tint in WEAPON_TINT.items():
        top = by_name[f'{w}_hit_3']
        perfect = scaled(top, f'{w}_perfect', 1.3)
        perfect['duration'] = max(perfect.get('duration', 1.0), 1.2)
        perfect['emitters'] += [
            ring('shock', tint, 40, 4.5, life=0.5, order=0),
            ring('shock_2', [1, 1, 1, 0.8], 30, 3.5, life=0.45, delay=0.1),
            sparks('stars', 'fx:star', [tint, [1, 1, 1, 1]], [10, 14], (120, 240), (12, 20), life=(0.5, 0.9), gravity=260,
                   spin=True),
        ]
        out.append(perfect)
        great = copy.deepcopy(by_name[f'{w}_hit_2'])
        great['name'] = f'{w}_great'
        great['emitters'].append(ring('ring', tint, 28, 3.0, life=0.35))
        out.append(great)
    out.append({'name': 'miss_puff', 'duration': 0.8, 'emitters': [
        {'name': 'puff', 'maxParticles': 6, 'emission': burst([4, 6]), 'shape': {'type': 'point', 'outward': True},
         'life': [0.4, 0.7], 'speed': [20, 50], 'size': [18, 28], 'rotation': [0, 360],
         'color': [[0.72, 0.7, 0.66, 0.8], [0.6, 0.58, 0.55, 0.7]], 'sprite': 'fx:smoke',
         'overLife': {'color': FADE, 'size': [{'t': 0, 'v': 0.6, 'ease': 'quadraticOut'}, {'t': 1, 'v': 1.6}]},
         'forces': {'drag': 2, 'gravity': [0, -30]}, 'render': {'blend': 'normal', 'order': 0}}]})
    return out


def status_effects():
    purple, white, blue, yellow = [0.85, 0.5, 1, 1], [1, 1, 1, 1], [0.7, 0.88, 1, 1], [1, 0.9, 0.35, 1]
    return [
        {'name': 'monster_special', 'duration': 1.0, 'emitters': [
            flash('glow', 'fx:glow', [0.8, 0.45, 1, 0.8], 120, life=0.8, order=-1),
            sparks('motes', 'fx:star', [purple, white], [12, 16], (40, 90), (8, 14), life=(0.5, 0.9), gravity=-140,
                   direction=-90, spread=70, spin=True),
            ring('ring', purple, 30, 3.5, life=0.5, delay=0.3)]},
        {'name': 'monster_transform', 'duration': 1.2, 'emitters': [
            flash('flash', 'fx:glow', white, 150, life=0.35, order=2),
            ring('ring', white, 36, 4.0, life=0.5, delay=0.05),
            ring('ring_2', [1, 0.7, 0.7, 1], 30, 3.0, life=0.5, delay=0.25),
            sparks('sparks', 'fx:spark', [white, [1, 0.75, 0.75, 1]], [16, 22], (160, 300), (10, 16), life=(0.3, 0.6))]},
        {'name': 'monster_parried', 'duration': 0.7, 'emitters': [
            flash('flash', 'fx:star', blue, 56, life=0.16),
            sparks('sparks', 'fx:spark', [blue, white], [12, 16], (180, 300), (10, 16), life=(0.2, 0.4), gravity=200),
            ring('ring', blue, 26, 3.0, life=0.3)]},
        {'name': 'monster_broken', 'duration': 1.2, 'emitters': [
            sparks('stars', 'fx:star', [yellow, white], [8, 10], (80, 160), (14, 22), life=(0.6, 1.0), gravity=180, spin=True),
            ring('dizzy', yellow, 34, 2.2, life=0.5, repeat=2, interval=0.2)]},
    ]


def move_effects():
    gold, white, violet, red = [1, 0.85, 0.4, 1], [1, 1, 1, 1], [0.75, 0.55, 1, 1], [1, 0.35, 0.55, 1]
    grey = [0.75, 0.78, 0.85, 1]
    return [
        {'name': 'star_trail', 'duration': 0.7, 'emitters': [
            flash('glint', 'fx:star', gold, 22, life=0.35),
            sparks('motes', 'fx:spark', [gold, white], [3, 5], (20, 60), (6, 10), life=(0.3, 0.6), gravity=-40)]},
        {'name': 'star_burst', 'duration': 0.9, 'emitters': [
            flash('flash', 'fx:star', gold, 60, life=0.25),
            ring('ring', gold, 26, 3.0, life=0.4),
            sparks('sparks', 'fx:spark', [gold, white], [10, 14], (120, 240), (8, 14), life=(0.3, 0.6), gravity=100)]},
        {'name': 'star_burst_big', 'duration': 1.2, 'emitters': [
            flash('flash', 'fx:glow', white, 120, life=0.3, order=2),
            ring('ring', gold, 36, 4.5, life=0.5),
            ring('ring_2', white, 26, 3.5, life=0.45, delay=0.08),
            sparks('stars', 'fx:star', [gold, white], [12, 16], (160, 300), (12, 20), life=(0.5, 0.9), gravity=220, spin=True)]},
        {'name': 'seal_trail', 'duration': 0.7, 'emitters': [
            flash('glint', 'fx:star', violet, 22, life=0.35),
            sparks('motes', 'fx:spark', [violet, red], [3, 5], (20, 60), (6, 10), life=(0.3, 0.6), gravity=-40)]},
        {'name': 'seal_burst', 'duration': 0.9, 'emitters': [
            flash('flash', 'fx:star', red, 60, life=0.25),
            ring('ring', violet, 26, 3.0, life=0.4),
            sparks('sparks', 'fx:spark', [violet, red], [10, 14], (120, 240), (8, 14), life=(0.3, 0.6), gravity=100)]},
        {'name': 'shield_crack', 'duration': 0.9, 'emitters': [
            flash('flash', 'fx:glow', grey, 90, life=0.2),
            ring('ring', grey, 30, 3.2, life=0.4),
            sparks('shards', 'fx:spark', [grey, white], [12, 16], (140, 260), (10, 16), life=(0.3, 0.6), gravity=420)]},
    ]


def mix(c, k):
    """c lighter toward white by k (0..1)."""
    return [round(c[i] + (1 - c[i]) * k, 3) for i in range(3)] + [c[3]]


def streaks(name, colour, length, count, life, delay=0, angle=0, size=(22, 30), order=1):
    """A slash's streak: long sparks laid along a line and turned along it, growing then shrinking, over a soft glow."""
    return {'name': name, 'maxParticles': count, 'emission': burst(count, delay), 'shape': {'type': 'line', 'length': length, 'angle': angle},
            'life': life, 'speed': [3, 6], 'direction': angle, 'spread': 2, 'size': [size[0] * 1.8, size[1] * 1.8], 'color': colour,
            'sprite': 'fx:spark',
            'overLife': {'color': FADE, 'size': [{'t': 0, 'v': 0.4, 'ease': 'backOut'}, {'t': 0.3, 'v': 1.4, 'ease': 'quadraticIn'}, {'t': 1, 'v': 0.3}]},
            'render': {'blend': 'add', 'alignToVelocity': True, 'order': order}}


def gather(name, colour, count, radius, pull):
    """Particles on a ring, pulled into its centre."""
    return {'name': name, 'maxParticles': count, 'emission': burst(count), 'shape': {'type': 'ring', 'radius': radius, 'thickness': 18},
            'life': [0.28, 0.4], 'speed': 0, 'size': [10, 16], 'color': [colour, [1, 1, 1, 1]], 'sprite': 'fx:spark',
            'overLife': {'color': [{'t': 0, 'v': [1, 1, 1, 0.3]}, {'t': 0.25, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0.4]}],
                         'size': [{'t': 0, 'v': 0.8}, {'t': 1, 'v': 1.4}]},
            'forces': {'radial': [-pull, -pull * 0.8], 'drag': 1},
            'render': {'blend': 'add', 'alignToVelocity': True, 'order': 1}}


def projectile(name, colour, head, trail_rate, trail_size):
    """A glowing head and a trail. Both are left in the world as the game moves the effect; the head's are short-lived, so
    they stay at the front. Endless: the game stops it where it lands."""
    return {'name': name, 'duration': 0, 'emitters': [
        {'name': 'head', 'maxParticles': 30, 'emission': {'rate': 90}, 'life': 0.1, 'speed': 0, 'size': [head, head * 1.15],
         'color': [mix(colour, 0.5), [1, 1, 1, 1]], 'sprite': 'fx:glow', 'overLife': {'color': FADE}, 'render': {'blend': 'add', 'order': 2}},
        {'name': 'core', 'maxParticles': 30, 'emission': {'rate': 90}, 'life': 0.08, 'speed': 0, 'size': [head * 0.45, head * 0.5],
         'color': [[1, 1, 1, 1], [1, 1, 1, 1]], 'sprite': 'fx:glow', 'overLife': {'color': FADE}, 'render': {'blend': 'add', 'order': 3}},
        {'name': 'trail', 'maxParticles': 80, 'emission': {'rate': trail_rate}, 'life': [0.25, 0.45], 'speed': [10, 40],
         'direction': 180, 'spread': 25, 'size': list(trail_size), 'color': [colour, mix(colour, 0.4)], 'sprite': 'fx:spark',
         'overLife': {'color': FADE, 'size': [{'t': 0, 'v': 1}, {'t': 1, 'v': 0.3}]}, 'forces': {'drag': 2},
         'render': {'blend': 'add', 'alignToVelocity': True, 'order': 1}}]}


def bigger(e, k):
    """`e` drawn k times as big: its particles' sizes and speeds, its shapes and pulls (not how many particles)."""
    for m in e['emitters']:
        for key in ('size', 'speed'):
            if isinstance(m.get(key), list):
                m[key] = [round(v * k, 1) for v in m[key]]
            elif isinstance(m.get(key), (int, float)):
                m[key] = round(m[key] * k, 1)
        sh = m.get('shape', {})
        for key in ('length', 'radius', 'thickness'):
            if key in sh:
                sh[key] = round(sh[key] * k, 1)
        if 'offset' in m:
            m['offset'] = [round(v * k, 1) for v in m['offset']]
        f = m.get('forces', {})
        if 'radial' in f:
            f['radial'] = [round(v * k, 1) for v in f['radial']]
        if 'gravity' in f:
            f['gravity'] = [round(v * k, 1) for v in f['gravity']]
    return e


# How much bigger than the old battle effects the new ones are drawn: the fighters are 300 px and more now.
FEEL_SCALE = {'wand_': 1.3, 'combo_spray_': 1.7, 'sword_slash': 1.6, 'bow_': 1.6, 'monster_': 2.2}


def feel_effects():
    gold, white, violet, red = [1, 0.85, 0.4, 1], [1, 1, 1, 1], [0.75, 0.55, 1, 1], [1, 0.35, 0.3, 1]
    orange, blue, magic = [1, 0.6, 0.2, 1], [0.6, 0.85, 1, 1], [0.8, 0.45, 1, 1]
    out = []
    # the staff's hints: violet to gold, more and bigger each tier
    for k in range(1, 5):
        c = [round(violet[i] + (gold[i] - violet[i]) * (k - 1) / 3, 3) for i in range(3)] + [1]
        e = {'name': f'wand_hint_{k}', 'duration': 0.9 + 0.1 * k, 'emitters': [
            flash('flash', 'fx:glow', mix(c, 0.3), 60 + 22 * k, life=0.3 + 0.05 * k, order=2),
            ring('ring', c, 26 + 6 * k, 2.2 + 0.5 * k, life=0.4 + 0.05 * k),
            sparks('sparks', 'fx:spark', [c, white], [4 + 5 * k, 6 + 6 * k], (90 + 40 * k, 180 + 60 * k), (7, 12), life=(0.3, 0.6), gravity=60)]}
        if k >= 2:
            e['emitters'].append(ring('ring_2', white, 20 + 4 * k, 2.0 + 0.6 * k, life=0.45, delay=0.08))
        if k >= 3:
            e['emitters'].append(sparks('stars', 'fx:star', [gold, white], [3 * k, 4 * k], (120, 260), (10, 18), life=(0.5, 0.9), gravity=200, spin=True))
        if k >= 4:
            e['emitters'].append(ring('shock', gold, 40, 5.0, life=0.55, delay=0.12, order=-1))
        out.append(e)
    out.append({'name': 'wand_vanish', 'duration': 0.6, 'emitters': [
        ring('ring', white, 40, 0.2, life=0.25),
        sparks('dust', 'fx:spark', [violet, white], [8, 10], (120, 200), (6, 10), life=(0.2, 0.4), gravity=0)]})
    out.append({'name': 'wand_return', 'duration': 0.5, 'emitters': [
        ring('ring', violet, 18, 2.2, life=0.3),
        flash('flash', 'fx:glow', mix(violet, 0.5), 50, life=0.18)]})
    # a hit's spray, tier by the combo
    for k in range(1, 6):
        e = {'name': f'combo_spray_{k}', 'duration': 1.0, 'emitters': [
            sparks('sparks', 'fx:spark', [gold, mix(orange, 0.3)], [6 + 8 * k, 10 + 10 * k], (140 + 40 * k, 260 + 70 * k), (7, 12 + k), life=(0.3, 0.7), gravity=240)]}
        if k >= 2:
            e['emitters'].append(sparks('stars', 'fx:star', [gold, white], [2 * k, 3 * k], (100, 220 + 30 * k), (9, 16), life=(0.4, 0.8), gravity=260, spin=True))
        if k >= 3:
            e['emitters'].append(ring('ring', gold, 24 + 4 * k, 2.5 + 0.4 * k, life=0.35))
        if k >= 4:
            e['emitters'].append(flash('flash', 'fx:glow', white, 70 + 20 * k, life=0.22, order=2))
        out.append(e)
    # the sword's slash: three afterimages along the cut, then sparks off it
    out.append({'name': 'sword_slash', 'duration': 0.8, 'emitters': [
        streaks('slash', [blue, white], 230, 16, [0.2, 0.26], order=3),
        streaks('after_1', [mix(blue, 0.2)[:3] + [0.7], blue], 210, 14, [0.22, 0.28], delay=0.05, order=2),
        streaks('after_2', [blue[:3] + [0.45], blue[:3] + [0.35]], 190, 12, [0.24, 0.3], delay=0.1, order=1),
        sparks('sparks', 'fx:spark', [blue, white], [12, 16], (180, 320), (7, 12), life=(0.2, 0.45), gravity=200, direction=90, spread=60)]})
    # the bow's pull and its arrow
    for k in range(1, 4):
        out.append({'name': f'bow_gather_{k}', 'duration': 0.5, 'emitters': [
            gather('in', gold if k < 3 else mix(gold, 0.3), 3 + 4 * k, 60 + 15 * k, 700 + 300 * k)] +
            ([flash('core', 'fx:glow', mix(gold, 0.4), 24 + 10 * k, life=0.12, order=2)] if k >= 2 else [])})
    out.append({'name': 'bow_explode', 'duration': 1.2, 'emitters': [
        flash('flash', 'fx:glow', white, 150, life=0.25, order=3),
        ring('ring', orange, 40, 4.5, life=0.45),
        ring('ring_2', gold, 30, 3.5, life=0.4, delay=0.06),
        sparks('sparks', 'fx:spark', [orange, mix(gold, 0.5)], [26, 34], (240, 460), (8, 14), life=(0.3, 0.7), gravity=260),
        {'name': 'smoke', 'maxParticles': 8, 'emission': burst([5, 8], 0.05), 'shape': {'type': 'point', 'outward': True},
         'life': [0.6, 1.0], 'speed': [30, 80], 'size': [30, 46], 'rotation': [0, 360], 'color': [[0.5, 0.45, 0.4, 0.6], [0.35, 0.32, 0.3, 0.5]],
         'sprite': 'fx:smoke', 'overLife': {'color': FADE, 'size': [{'t': 0, 'v': 0.6, 'ease': 'quadraticOut'}, {'t': 1, 'v': 1.8}]},
         'forces': {'drag': 2, 'gravity': [0, -40]}, 'render': {'blend': 'normal', 'order': 0}}]})
    # the monsters' attacks
    out.append({'name': 'monster_claw', 'duration': 0.8, 'emitters': [
        streaks('claw_1', [red, mix(red, 0.5)], 150, 10, [0.18, 0.24], angle=-55, size=(16, 22), order=2),
        streaks('claw_2', [red, mix(red, 0.5)], 160, 10, [0.18, 0.24], delay=0.04, angle=-55, size=(16, 22), order=2) | {'offset': [26, 0]},
        streaks('claw_3', [red, mix(red, 0.5)], 150, 10, [0.18, 0.24], delay=0.08, angle=-55, size=(16, 22), order=2) | {'offset': [52, 0]},
        sparks('sparks', 'fx:spark', [red, white], [10, 14], (140, 260), (7, 12), life=(0.2, 0.45), gravity=240)]})
    out.append({'name': 'monster_cast', 'duration': 0.7, 'emitters': [
        flash('glow', 'fx:glow', mix(red, 0.2), 130, life=0.45, order=-1),
        gather('in', orange, 12, 90, 900)]})
    out.append({'name': 'monster_cast_magic', 'duration': 0.7, 'emitters': [
        flash('glow', 'fx:glow', mix(magic, 0.2), 130, life=0.45, order=-1),
        gather('in', magic, 14, 90, 900),
        ring('ring', magic, 30, 2.6, life=0.4)]})
    out.append(projectile('monster_bolt', orange, 42, 110, (8, 14)))
    out.append(projectile('monster_bolt_big', red, 66, 170, (11, 18)))
    out.append(projectile('monster_orb', magic, 52, 130, (9, 15)))
    for name, c in (('monster_blast', orange), ('monster_blast_magic', magic)):
        out.append({'name': name, 'duration': 1.1, 'emitters': [
            flash('flash', 'fx:glow', mix(c, 0.4), 170, life=0.25, order=3),
            ring('ring', c, 40, 4.5, life=0.45),
            ring('ring_2', white, 28, 3.2, life=0.4, delay=0.07),
            sparks('sparks', 'fx:spark', [c, white], [22, 30], (220, 420), (8, 14), life=(0.3, 0.65), gravity=260)]})
    out.append({'name': 'monster_smash', 'duration': 1.2, 'emitters': [
        flash('flash', 'fx:glow', mix(red, 0.35), 190, life=0.28, order=3),
        ring('ring', red, 46, 5.0, life=0.5),
        ring('ring_2', orange, 34, 3.8, life=0.45, delay=0.08),
        streaks('slash', [red, white], 260, 18, [0.2, 0.28], angle=-35, size=(26, 34), order=2),
        sparks('sparks', 'fx:spark', [red, mix(orange, 0.4)], [26, 34], (240, 460), (9, 15), life=(0.3, 0.7), gravity=300),
        sparks('stars', 'fx:star', [orange, white], [6, 9], (120, 240), (12, 20), life=(0.5, 0.9), gravity=260, spin=True)]})
    for e in out:
        k = next((v for p, v in FEEL_SCALE.items() if e['name'].startswith(p)), 1.0)
        bigger(e, k)
    return out


def main():
    with open(OUT, encoding='utf-8') as f:
        data = json.load(f)
    by_name = {e['name']: e for e in data['effects']}
    made = result_effects(by_name) + status_effects() + move_effects() + feel_effects()
    names = {e['name'] for e in made}
    data['effects'] = [e for e in data['effects'] if e['name'] not in names] + made
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=1, ensure_ascii=False) + '\n')
    print(f'{os.path.relpath(OUT, ROOT)}: {len(made)} effects made ({", ".join(e["name"] for e in made)})')


if __name__ == '__main__':
    main()
