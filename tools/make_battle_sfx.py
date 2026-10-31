"""make_battle_sfx.py -- writes the battle's moment sounds into assets/media/sfx (docs/gameplay/15_BATTLE_REVAMP.md §3.8):
one per result and moment, named in data/battle.json "feel" "sounds":

  battle_hit       a short thud                  battle_great     a brighter knock with a ring
  battle_perfect   a three-note chime            battle_miss      a dull falling buzz
  battle_parry     a metal clang                 battle_block     a short wooden knock
  battle_break     a deep boom and a crack       battle_ultimate  a rising fanfare of three notes
  battle_phase     a low rumble (a monster's phase change)
  battle_win       a four-note arpeggio

Synthesised (sines, a little noise, envelopes), 16-bit mono 44.1 kHz like the other sounds; made again by running it.

  python tools/make_battle_sfx.py
"""
import math, os, random, struct, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'media', 'sfx')
RATE = 44100


def env(t, attack, decay):
    return min(1.0, t / max(attack, 1e-4)) * math.exp(-t / decay)


def tone(t, freq, slide=1.0, length=1.0):
    f = freq * (slide ** min(1.0, t / max(length, 1e-4)))   # an exponential glide over `length`
    return math.sin(2 * math.pi * f * t)


rng = random.Random(1031)


def noise():
    return rng.uniform(-1.0, 1.0)


def note_at(t, start, freq, decay=0.12, vol=1.0):
    if t < start:
        return 0.0
    u = t - start
    return vol * env(u, 0.004, decay) * (0.8 * math.sin(2 * math.pi * freq * u) + 0.2 * math.sin(4 * math.pi * freq * u))


SOUNDS = {
    'battle_hit':      (0.16, lambda t: 0.5 * env(t, 0.002, 0.05) * (0.7 * tone(t, 180, 0.5, 0.12) + 0.3 * noise())),
    'battle_great':    (0.25, lambda t: 0.45 * env(t, 0.002, 0.07) * (0.6 * tone(t, 260, 0.6, 0.15) + 0.2 * noise())
                                        + 0.25 * env(t, 0.005, 0.12) * math.sin(2 * math.pi * 1320 * t)),
    'battle_perfect':  (0.55, lambda t: 0.35 * (note_at(t, 0.0, 1047, 0.2) + note_at(t, 0.06, 1319, 0.2) + note_at(t, 0.12, 1568, 0.3))
                                        + 0.4 * env(t, 0.002, 0.05) * tone(t, 200, 0.5, 0.1)),
    'battle_miss':     (0.22, lambda t: 0.3 * env(t, 0.01, 0.1) * (0.6 * tone(t, 160, 0.6, 0.2) + 0.4 * noise())),
    'battle_parry':    (0.35, lambda t: 0.35 * env(t, 0.001, 0.12) * (math.sin(2 * math.pi * 2100 * t) + 0.7 * math.sin(2 * math.pi * 3170 * t)
                                                                     + 0.5 * math.sin(2 * math.pi * 4620 * t)) / 2.2),
    'battle_block':    (0.12, lambda t: 0.45 * env(t, 0.001, 0.03) * (0.6 * tone(t, 420, 0.7, 0.08) + 0.4 * noise())),
    'battle_break':    (0.8,  lambda t: 0.55 * env(t, 0.003, 0.3) * tone(t, 90, 0.4, 0.6)
                                        + 0.3 * env(t, 0.001, 0.06) * noise()),
    'battle_ultimate': (0.7,  lambda t: 0.3 * (note_at(t, 0.0, 392, 0.12) + note_at(t, 0.09, 523, 0.12) + note_at(t, 0.18, 784, 0.4))),
    'battle_phase':    (0.9,  lambda t: 0.4 * env(t, 0.05, 0.4) * (0.7 * tone(t, 70, 0.7, 0.8) + 0.3 * noise())),
    'battle_win':      (0.9,  lambda t: 0.3 * (note_at(t, 0.0, 523, 0.25) + note_at(t, 0.11, 659, 0.25) + note_at(t, 0.22, 784, 0.25)
                                               + note_at(t, 0.33, 1047, 0.45))),
}


def write(name, length, fn):
    n = int(RATE * length)
    peak = max(abs(fn(i / RATE)) for i in range(n)) or 1.0
    scale = min(1.0, 0.85 / peak)
    frames = b''.join(struct.pack('<h', int(max(-1.0, min(1.0, fn(i / RATE) * scale)) * 32767)) for i in range(n))
    with wave.open(os.path.join(OUT, name + '.wav'), 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)


def main():
    for name, (length, fn) in SOUNDS.items():
        write(name, length, fn)
        print('wrote', os.path.join('assets', 'media', 'sfx', name + '.wav'))


if __name__ == '__main__':
    main()
