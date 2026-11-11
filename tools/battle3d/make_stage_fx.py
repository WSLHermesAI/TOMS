"""make_stage_fx.py -- the 3D battle stages' own particle effects in assets/media/fx/battle.particle (docs/gameplay/21 §4):

    stage_fire    a brazier's fire, looping: a flickering flame, embers rising, a little smoke

Sizes are pixels at 100 px a metre: the game draws it at its place on the stage, scaled by the distance
(Game::f_DrawBattle3DStageFx). Run again after changing it; open the file in particle_editor to look at it.
"""
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, 'assets', 'media', 'fx', 'battle.particle')


def stage_fire():
    flame = {
        'name': 'flame', 'maxParticles': 60, 'emission': {'rate': 55}, 'shape': {'type': 'box', 'size': [46, 8]},
        'life': [0.3, 0.55], 'speed': [40, 95], 'direction': -90, 'spread': 18, 'size': [34, 58], 'rotation': [0, 360],
        'color': [[1, 0.62, 0.22, 0.9], [1, 0.45, 0.12, 0.85]], 'sprite': 'fx:glow',
        'overLife': {'color': [{'t': 0, 'v': [1, 1, 0.85, 0.3]}, {'t': 0.15, 'v': [1, 1, 1, 1]}, {'t': 0.6, 'v': [1, 0.7, 0.5, 0.75]},
                               {'t': 1, 'v': [0.9, 0.35, 0.2, 0], 'ease': 'quadraticIn'}],
                     'size': [{'t': 0, 'v': 0.7}, {'t': 0.3, 'v': 1}, {'t': 1, 'v': 0.25}]},
        'forces': {'drag': 1.5, 'gravity': [0, -60]},
        'render': {'blend': 'add', 'order': 1}}
    core = {
        'name': 'core', 'maxParticles': 12, 'emission': {'rate': 14}, 'shape': {'type': 'box', 'size': [24, 4]},
        'life': [0.25, 0.4], 'speed': [15, 35], 'direction': -90, 'spread': 10, 'size': [26, 40],
        'color': [[1, 0.92, 0.6, 1], [1, 0.85, 0.5, 1]], 'sprite': 'fx:glow',
        'overLife': {'color': [{'t': 0, 'v': [1, 1, 1, 0.4]}, {'t': 0.3, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0]}]},
        'render': {'blend': 'add', 'order': 2}}
    embers = {
        'name': 'embers', 'maxParticles': 30, 'emission': {'rate': 9}, 'shape': {'type': 'box', 'size': [40, 6]},
        'life': [0.9, 1.8], 'speed': [60, 140], 'direction': -90, 'spread': 35, 'size': [3, 6],
        'color': [[1, 0.75, 0.3, 1], [1, 0.5, 0.15, 1]], 'sprite': 'fx:glow',
        'overLife': {'color': [{'t': 0, 'v': [1, 1, 1, 1]}, {'t': 0.7, 'v': [1, 0.8, 0.6, 0.9]}, {'t': 1, 'v': [1, 0.5, 0.3, 0]}]},
        'forces': {'drag': 1.2, 'gravity': [0, -25]},
        'render': {'blend': 'add', 'order': 3}}
    smoke = {
        'name': 'smoke', 'maxParticles': 16, 'emission': {'rate': 5}, 'shape': {'type': 'box', 'size': [30, 6]},
        'life': [1.4, 2.4], 'speed': [25, 45], 'direction': -90, 'spread': 15, 'size': [30, 50], 'rotation': [0, 360],
        'spin': [-30, 30], 'color': [[0.2, 0.19, 0.22, 0.32], [0.16, 0.15, 0.18, 0.28]], 'sprite': 'fx:smoke',
        'overLife': {'color': [{'t': 0, 'v': [1, 1, 1, 0]}, {'t': 0.25, 'v': [1, 1, 1, 1]}, {'t': 1, 'v': [1, 1, 1, 0]}],
                     'size': [{'t': 0, 'v': 0.8}, {'t': 1, 'v': 2.6}]},
        'forces': {'drag': 0.6, 'gravity': [0, -12]},
        'render': {'blend': 'normal', 'order': 0}}
    return {'name': 'stage_fire', 'duration': 4, 'loop': True, 'emitters': [smoke, flame, core, embers]}


def main():
    with open(OUT, encoding='utf-8') as f:
        data = json.load(f)
    made = [stage_fire()]
    names = {e['name'] for e in made}
    data['effects'] = [e for e in data['effects'] if e['name'] not in names] + made
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write(json.dumps(data, indent=1, ensure_ascii=False) + '\n')
    print(f'{os.path.relpath(OUT, ROOT)}: {", ".join(names)}')


if __name__ == '__main__':
    main()
