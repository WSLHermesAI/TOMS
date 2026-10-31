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


def main():
    with open(OUT, encoding='utf-8') as f:
        data = json.load(f)
    by_name = {e['name']: e for e in data['effects']}
    made = result_effects(by_name) + status_effects() + move_effects()
    names = {e['name'] for e in made}
    data['effects'] = [e for e in data['effects'] if e['name'] not in names] + made
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=1, ensure_ascii=False) + '\n')
    print(f'{os.path.relpath(OUT, ROOT)}: {len(made)} effects made ({", ".join(e["name"] for e in made)})')


if __name__ == '__main__':
    main()
