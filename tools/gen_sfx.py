#!/usr/bin/env python3
# Generate 6 CC0-style SFX as 16-bit PCM mono WAV files for the game.
# No external assets / no licensing issues -- fully synthesized.
import math, struct, wave, os

SR = 44100
OUT = os.path.join(os.path.dirname(__file__), "..", "assets", "sfx")

def note(freq, dur, t0, kind="sine", vol=0.6, attack=0.005, decay=None):
    """Append a tone into the global buffer. kind: sine/square/saw/noise."""
    n = int(SR * dur)
    decay = decay if decay is not None else dur
    for i in range(n):
        t = i / SR
        e = 0.0
        if t < attack:
            e = t / attack
        else:
            e = max(0.0, 1.0 - (t - attack) / max(1e-4, decay - attack))
        env = e * vol
        if kind == "sine":
            s = math.sin(2*math.pi*freq*t)
        elif kind == "square":
            s = 1.0 if math.sin(2*math.pi*freq*t) >= 0 else -1.0
        elif kind == "saw":
            s = 2.0*(freq*t - math.floor(freq*t + 0.5))
        elif kind == "noise":
            s = (hash((i+1)*2654435761 & 0xffffffff) / 2**31 - 1.0)
        else:
            s = 0.0
        buf.append(int(max(-1.0, min(1.0, s*env)) * 32767))

def write(name, bufs):
    p = os.path.join(OUT, name + ".wav")
    with wave.open(p, "w") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", x) for x in bufs))
    print("wrote", p, len(bufs), "samples")

def sweep(f0, f1, dur, kind="saw", vol=0.6):
    """Frequency sweep, e.g. attack swing."""
    global buf
    buf = []
    n = int(SR*dur)
    for i in range(n):
        t = i/SR
        f = f0 + (f1 - f0) * (i/n)
        e = max(0.0, 1.0 - t/dur)
        if kind == "saw":
            s = 2.0*(f*t - math.floor(f*t + 0.5))
        else:
            s = math.sin(2*math.pi*f*t)
        buf.append(int(max(-1.0,min(1.0, s*e*vol))*32767))
    return buf

os.makedirs(OUT, exist_ok=True)

# 1) walk -- soft low step thud
buf = []
note(120, 0.07, 0, kind="sine", vol=0.5, attack=0.003, decay=0.07)
write("walk", buf)

# 2) get_item -- pleasant two-note blip (coin-ish)
buf = []
note(660, 0.10, 0.0, kind="sine", vol=0.5, decay=0.10)
note(990, 0.12, 0.09, kind="sine", vol=0.5, decay=0.12)
write("get_item", buf)

# 3) player_attack -- downward swing + tick
buf = sweep(420, 120, 0.12, kind="saw", vol=0.55)
write("player_attack", buf)

# 4) enemy_attack -- lower, noisier thud
buf = []
buf += sweep(200, 70, 0.13, kind="square", vol=0.5)
note(90, 0.10, 0.0, kind="noise", vol=0.25, decay=0.10)
write("enemy_attack", buf)

# 5) dialogue_popup -- UI pop
buf = []
note(880, 0.09, 0.0, kind="sine", vol=0.45, attack=0.002, decay=0.09)
write("dialogue_popup", buf)

# 6) confirm_click -- short high tick
buf = []
note(1200, 0.045, 0.0, kind="sine", vol=0.5, attack=0.001, decay=0.045)
note(1200, 0.03, 0.05, kind="noise", vol=0.15, decay=0.03)
write("confirm_click", buf)

print("done")
