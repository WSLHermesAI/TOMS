"""make_battle_sfx.py -- writes the battle's moment sounds into assets/media/sfx (docs/gameplay/15_BATTLE_REVAMP.md §3.8):
one per result and moment, named in data/battle.json "feel" "sounds":

  battle_hit       a short thud                  battle_great     a brighter knock with a ring
  battle_perfect   a three-note chime            battle_miss      a dull falling buzz
  battle_parry     a metal clang                 battle_block     a short wooden knock
  battle_break     a deep boom and a crack       battle_ultimate  a rising fanfare of three notes
  battle_phase     a low rumble (a monster's phase change)
  battle_win       a four-note arpeggio

The attack feel (docs/gameplay/20, data/battle.json "feelFx"; the game plays several at other pitches):

  wand_hint        a bell (the staff's hints, a note higher each)   wand_vanish    a falling sparkle and a swoosh
  wand_cast        a rising zap (each spell)                         sword_slash    a sweeping swish
  bow_charge       a short hum (the pull, higher as it charges)      explosion      a low boom and a rumble
  monster_launch   a rising whoosh (something thrown)                magic_cast     a wavering dark chord
  monster_impact   a thud and a crunch

Synthesised (sines, a little noise, envelopes), mono 44.1 kHz OGG like the other sounds; made again by running it.

  python tools/make_battle_sfx.py
"""
import math, os, random, struct, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools', 'audio'))
import ogg  # noqa: E402
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


class OnePole:
    """A one-pole low-pass, its state reset at t = 0 (each sound is made twice: once for its peak, once to write)."""
    def __init__(self):
        self.y = 0.0

    def __call__(self, x, t, cutoff):
        if t == 0:
            self.y = 0.0
        a = 1.0 - math.exp(-2 * math.pi * cutoff / RATE)
        self.y += a * (x - self.y)
        return self.y


def sweep(t, f0, f1, length):
    """A frequency gliding from f0 to f1 over `length` (exponential)."""
    return f0 * (f1 / f0) ** min(1.0, t / max(length, 1e-4))


LP = {k: OnePole() for k in ('slash', 'slash_hp', 'boom', 'launch', 'hit')}


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
    # the attack feel (docs/gameplay/20); appended, so the sounds above keep their noise and stay the same
    'wand_hint':       (0.6,  lambda t: 0.35 * env(t, 0.002, 0.18) * (math.sin(2 * math.pi * 1760 * t) + 0.3 * math.sin(2 * math.pi * 3520 * t))
                                        + 0.18 * env(t, 0.002, 0.06) * math.sin(2 * math.pi * 1760 * 2.76 * t)),
    'wand_vanish':     (0.4,  lambda t: 0.3 * (note_at(t, 0.0, 2093, 0.06) + note_at(t, 0.05, 1568, 0.06) + note_at(t, 0.1, 1047, 0.1))
                                        + 0.2 * env(t, 0.01, 0.12) * noise()),
    'wand_cast':       (0.35, lambda t: 0.4 * env(t, 0.003, 0.12) * tone(t, 500, 3.2, 0.18) + 0.12 * env(t, 0.001, 0.04) * noise()),
    'sword_slash':     (0.26, lambda t: 0.9 * env(t, 0.015, 0.08) * (lambda lo: lo - LP['slash_hp'](lo, t, 600))(LP['slash'](noise(), t, sweep(t, 6000, 1200, 0.2)))
                                        + 0.08 * env(t, 0.005, 0.05) * math.sin(2 * math.pi * 2400 * t)),
    'bow_charge':      (0.14, lambda t: 0.35 * env(t, 0.02, 0.06) * (math.sin(2 * math.pi * 330 * t) + 0.5 * math.sin(2 * math.pi * 660 * t))),
    'explosion':       (0.9,  lambda t: 0.9 * env(t, 0.002, 0.25) * LP['boom'](noise(), t, sweep(t, 900, 200, 0.6))
                                        + 0.6 * env(t, 0.003, 0.3) * tone(t, 70, 0.5, 0.5)),
    'monster_launch':  (0.4,  lambda t: 0.8 * env(t, 0.05, 0.15) * LP['launch'](noise(), t, sweep(t, 400, 3000, 0.35))
                                        + 0.2 * env(t, 0.02, 0.2) * tone(t, 140, 2.0, 0.35)),
    'magic_cast':      (0.55, lambda t: 0.3 * env(t, 0.05, 0.25) * (0.5 + 0.5 * math.sin(2 * math.pi * 18 * t))
                                        * (math.sin(2 * math.pi * 330 * t) + math.sin(2 * math.pi * 336 * t) + 0.6 * math.sin(2 * math.pi * 495 * t))),
    'monster_impact':  (0.4,  lambda t: 0.7 * env(t, 0.001, 0.12) * tone(t, 110, 0.45, 0.2) + 0.6 * env(t, 0.001, 0.05) * LP['hit'](noise(), t, 2500)),
}


def write(name, length, fn):
    n = int(RATE * length)
    peak = max(abs(fn(i / RATE)) for i in range(n)) or 1.0
    scale = min(1.0, 0.85 / peak)
    frames = b''.join(struct.pack('<h', int(max(-1.0, min(1.0, fn(i / RATE) * scale)) * 32767)) for i in range(n))
    ogg.pcm_to_ogg(os.path.join(OUT, name + '.ogg'), frames, RATE)


def main():
    only = set(sys.argv[1:])   # names: only those (the others keep their files)
    for name, (length, fn) in SOUNDS.items():
        if only and name not in only:
            continue
        write(name, length, fn)
        print('wrote', os.path.join('assets', 'media', 'sfx', name + '.ogg'))


if __name__ == '__main__':
    main()
