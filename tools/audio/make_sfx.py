#!/usr/bin/env python3
"""make_sfx.py -- makes sound effects from short recipes, with plain maths: no AI, no download, no licence to check
(docs/engine/22_AUDIO_GENERATION.md).

    python tools/audio/make_sfx.py --list          the recipes in tools/audio/sfx_recipes.json
    python tools/audio/make_sfx.py hit_heavy coin  makes these
    python tools/audio/make_sfx.py --all           makes every one
    python tools/audio/make_sfx.py deny --to-game  writes it into assets/media/sfx, at the game's level (a new one also
                                                   needs its sfx.<name> key: docs/engine/22_AUDIO_GENERATION.md)

Writes Build/art_out/audio/sfx/<name>.ogg (44.1 kHz mono). Nothing goes into the game until it is copied there.
A new sound: add a recipe to the file and run this again. --seed gives another variation of the noisy ones.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np

import ogg

ROOT = Path(__file__).resolve().parents[2]
RECIPES = Path(__file__).with_name('sfx_recipes.json')
OUT = ROOT / 'Build' / 'art_out' / 'audio' / 'sfx'
GAME_SFX = ROOT / 'assets' / 'media' / 'sfx'
RATE = 44100
PEAK = 0.89
GAME_PEAK = 0.5   # about where the game's other sounds peak


def sweep(value, count):
    """A number, or [from, to] sliding along the sound (evenly in pitch, as the ear hears it)."""
    if isinstance(value, (int, float)):
        return np.full(count, float(value))
    return np.geomspace(max(value[0], 1.0), max(value[1], 1.0), count)


def one_pole_low(signal, cutoff):
    """Lets through what is below `cutoff` (Hz, for each sample)."""
    k = 1.0 - np.exp(-2.0 * np.pi * cutoff / RATE)
    out = np.empty_like(signal)
    y = 0.0
    for i, x in enumerate(signal):
        y += k[i] * (x - y)
        out[i] = y
    return out


def layer(spec, rng):
    count = max(1, int(spec['ms'] * RATE / 1000))
    kind = spec['wave']
    if kind == 'noise':
        signal = rng.uniform(-1.0, 1.0, count)
    else:
        phase = np.cumsum(sweep(spec['pitch'], count)) / RATE
        if kind == 'sine':
            signal = np.sin(2.0 * np.pi * phase)
        elif kind == 'square':
            signal = np.where(phase % 1.0 < 0.5, 1.0, -1.0)
        elif kind == 'saw':
            signal = 2.0 * (phase % 1.0) - 1.0
        else:
            sys.exit(f'unknown wave "{kind}"')
    if 'low' in spec:
        signal = one_pole_low(signal, sweep(spec['low'], count))
    if 'high' in spec:
        signal = signal - one_pole_low(signal, sweep(spec['high'], count))
    attack = min(count, max(1, int(spec.get('attack', 1) * RATE / 1000)))
    envelope = (1.0 - np.linspace(0.0, 1.0, count)) ** spec.get('curve', 2.0)
    envelope[:attack] *= np.linspace(0.0, 1.0, attack)
    return signal * envelope * spec.get('gain', 1.0)


def render(layers, seed, peak=PEAK):
    rng = np.random.default_rng(seed)
    total = max(int((l.get('at', 0) + l['ms']) * RATE / 1000) + 1 for l in layers)
    mix = np.zeros(total)
    for spec in layers:
        part = layer(spec, rng)
        start = int(spec.get('at', 0) * RATE / 1000)
        mix[start:start + len(part)] += part
    return mix / max(float(np.abs(mix).max()), 1e-6) * peak   # the same top level for all


def write(name, mix, folder=OUT):
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / f'{name}.ogg'
    ogg.pcm_to_ogg(path, (mix * 32767).astype('<i2').tobytes(), RATE)
    print(f'{path}  {len(mix) / RATE * 1000:.0f} ms, {path.stat().st_size / 1000:.1f} KB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('names', nargs='*')
    p.add_argument('--all', action='store_true')
    p.add_argument('--list', action='store_true')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--to-game', action='store_true', help='into assets/media/sfx, at the level of the game\'s sounds')
    args = p.parse_args()
    recipes = {k: v for k, v in json.loads(RECIPES.read_text(encoding='utf-8')).items() if not k.startswith('_')}
    if args.list or not (args.names or args.all):
        print('recipes:', ', '.join(recipes))
        return
    for name in (recipes if args.all else args.names):
        if name not in recipes:
            sys.exit(f'no recipe "{name}"; the recipes: {", ".join(recipes)}')
        if args.to_game:
            write(name, render(recipes[name], args.seed, GAME_PEAK), GAME_SFX)
        else:
            write(name, render(recipes[name], args.seed))


if __name__ == '__main__':
    main()
