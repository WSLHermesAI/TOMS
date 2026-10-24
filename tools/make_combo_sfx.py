"""make_combo_sfx.py -- writes assets/media/sfx/combo_ready.wav: the battle's "tap now" sound, played when the attack
track's marker enters a point's window (Game::comboHint; docs/gameplay/07_GAMEPLAY_PLAN.md §17).

A short bright "ting": two soft tones (G6 and D7), a 3 ms attack and a quick fall, 16-bit mono 44.1 kHz like the other
sounds in assets/media/sfx. Short on purpose: it plays on every point, so it must not blur into the next one.

  python tools/make_combo_sfx.py
"""
import math, os, struct, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, 'assets', 'media', 'sfx', 'combo_ready.wav')
RATE = 44100
LENGTH = 0.12      # seconds
VOLUME = 0.32      # of full scale (the other sounds peak about here)


def sample(t):
    attack = min(1.0, t / 0.003)
    fall = math.exp(-t / 0.035)
    tone = 0.75 * math.sin(2 * math.pi * 1568.0 * t) + 0.25 * math.sin(2 * math.pi * 2349.3 * t)
    return VOLUME * attack * fall * tone


def main():
    n = int(RATE * LENGTH)
    frames = b''.join(struct.pack('<h', int(max(-1.0, min(1.0, sample(i / RATE))) * 32767)) for i in range(n))
    with wave.open(OUT, 'wb') as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
    print('wrote', os.path.relpath(OUT, ROOT), '(%d ms)' % int(LENGTH * 1000))


if __name__ == '__main__':
    main()
