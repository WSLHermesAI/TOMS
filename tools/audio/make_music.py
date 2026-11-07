#!/usr/bin/env python3
"""make_music.py -- makes a piece of game music with ACE-Step 1.5 in the local ComfyUI (docs/engine/22_AUDIO_GENERATION.md).

    python tools/audio/make_music.py --name forest_battle --seconds 30 --bpm 140 --key "A minor" ^
        --tags "fantasy RPG battle music, instrumental, taiko drums, fast strings, wooden flute, energetic"

Writes Build/art_out/audio/music/<name>_<seed>.ogg (48 kHz stereo, 160 kbps). Nothing goes into the game until it is copied there.
By default the end is crossfaded into the start, so the track loops; --no-loop keeps it as made.
Change --seed for another take of the same words. Runs entirely on this PC.

It starts the ComfyUI of AIGamestyle/_comfyui if nothing answers at 127.0.0.1:8188, and stops it again afterwards.
Needs AIGamestyle/_comfyui/ComfyUI/models/checkpoints/ace_step_1.5_turbo_aio.safetensors.
"""
import argparse
import json
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

import numpy as np

import ogg

ROOT = Path(__file__).resolve().parents[2]
COMFY = ROOT / 'AIGamestyle' / '_comfyui' / 'ComfyUI'
VENV_PY = COMFY / '.venv' / 'Scripts' / 'python.exe'
CHECKPOINT = 'ace_step_1.5_turbo_aio.safetensors'
SERVER = 'http://127.0.0.1:8188'
OUT = ROOT / 'Build' / 'art_out' / 'audio' / 'music'
LOOP_FADE_S = 1.5
EXTRA_S = 6.0


def answering():
    try:
        urllib.request.urlopen(SERVER + '/system_stats', timeout=3).read()
        return True
    except OSError:
        return False


def start_server():
    print('starting ComfyUI (hidden) ...', flush=True)
    # --disable-all-custom-nodes: music needs none of the art add-ons, and they start slowly.
    log = open(ROOT / 'Build' / 'art_out' / 'audio' / 'comfyui.log', 'w', encoding='utf-8', errors='replace')
    process = subprocess.Popen([str(VENV_PY), 'main.py', '--disable-auto-launch', '--port', '8188', '--disable-all-custom-nodes'], cwd=COMFY, stdout=log, stderr=log,
                               creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    end = time.time() + 240
    while time.time() < end:
        if process.poll() is not None:
            sys.exit('ComfyUI stopped while starting: see Build/art_out/audio/comfyui.log')
        if answering():
            return process
        time.sleep(3)
    process.terminate()
    sys.exit('ComfyUI did not answer in 4 minutes: see Build/art_out/audio/comfyui.log')


def graph(args, seed, seconds):
    """The official "ACE-Step 1.5" workflow (Comfy-Org workflow_templates, audio_ace_step_1_5_checkpoint) with its settings."""
    return {
        '1': {'class_type': 'CheckpointLoaderSimple', 'inputs': {'ckpt_name': CHECKPOINT}},
        '2': {'class_type': 'ModelSamplingAuraFlow', 'inputs': {'model': ['1', 0], 'shift': 3.0}},
        '3': {'class_type': 'TextEncodeAceStepAudio1.5', 'inputs': {
            'clip': ['1', 1], 'tags': args.tags, 'lyrics': args.lyrics, 'seed': seed, 'bpm': args.bpm, 'duration': seconds,
            'timesignature': args.beats, 'language': 'en', 'keyscale': args.key, 'generate_audio_codes': not args.no_audio_codes,
            'cfg_scale': 2.0, 'temperature': args.temperature, 'top_p': args.top_p, 'top_k': 0, 'min_p': 0.0}},
        '4': {'class_type': 'ConditioningZeroOut', 'inputs': {'conditioning': ['3', 0]}},
        '5': {'class_type': 'EmptyAceStep1.5LatentAudio', 'inputs': {'seconds': seconds, 'batch_size': 1}},
        '6': {'class_type': 'KSampler', 'inputs': {
            'model': ['2', 0], 'positive': ['3', 0], 'negative': ['4', 0], 'latent_image': ['5', 0], 'seed': seed,
            'steps': args.steps, 'cfg': 1.0, 'sampler_name': 'euler', 'scheduler': 'simple', 'denoise': 1.0}},
        '7': {'class_type': 'VAEDecodeAudio', 'inputs': {'samples': ['6', 0], 'vae': ['1', 2]}},
        '8': {'class_type': 'SaveAudio', 'inputs': {'audio': ['7', 0], 'filename_prefix': 'toms_music/' + args.name}},
    }


def run_graph(prompt):
    body = json.dumps({'prompt': prompt}).encode('utf-8')
    request = urllib.request.Request(SERVER + '/prompt', body, {'Content-Type': 'application/json'})
    try:
        prompt_id = json.load(urllib.request.urlopen(request, timeout=30))['prompt_id']
    except urllib.error.HTTPError as error:
        sys.exit('ComfyUI refused the workflow: ' + error.read().decode('utf-8', 'replace')[:800])
    started = time.time()
    while True:
        time.sleep(2)
        history = json.load(urllib.request.urlopen(f'{SERVER}/history/{prompt_id}', timeout=30))
        if prompt_id not in history:
            if time.time() - started > 1800:
                sys.exit('no result in 30 minutes')
            continue
        entry = history[prompt_id]
        if entry.get('status', {}).get('status_str') == 'error':
            sys.exit('ComfyUI failed: ' + json.dumps(entry['status'].get('messages', []))[:1200])
        print(f'generated in {time.time() - started:.0f} s', flush=True)
        return entry['outputs']['8']['audio'][0]


def fetch(info, path):
    query = urllib.parse.urlencode({'filename': info['filename'], 'subfolder': info.get('subfolder', ''), 'type': info.get('type', 'output')})
    path.write_bytes(urllib.request.urlopen(f'{SERVER}/view?{query}', timeout=120).read())


DECODE = r'''
import sys, av, numpy as np
c = av.open(sys.argv[1]); s = c.streams.audio[0]
r = av.AudioResampler(format='fltp', layout='stereo', rate=48000)
out = []
for frame in c.decode(s):
    for f in r.resample(frame):
        out.append(f.to_ndarray())
np.save(sys.argv[2], np.concatenate(out, axis=1).T)
'''


def decode(flac, npy):
    """To samples, with the av of ComfyUI's venv (this project's Python has no FLAC reader)."""
    subprocess.run([str(VENV_PY), '-c', DECODE, str(flac), str(npy)], check=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    return np.load(npy)


def trim_silence(samples, rate=48000, level=0.01):
    """The model often ends the song early and leaves silence after it: this cuts the quiet off both ends."""
    step = rate // 20
    loud = [i for i in range(0, len(samples) - step, step) if np.sqrt(np.mean(samples[i:i + step] ** 2)) >= level]
    if not loud:
        return samples
    return samples[loud[0]:loud[-1] + step]


def make_loop(samples, seconds, rate=48000):
    """The extra piece at the end fades out over the start as the start fades in (equal power), so the end meets the start."""
    fade = int(LOOP_FADE_S * rate)
    keep = min(int(seconds * rate), len(samples) - fade)
    t = np.linspace(0.0, np.pi / 2, fade)[:, None]
    out = samples[:keep].copy()
    out[:fade] = samples[:fade] * np.sin(t) + samples[keep:keep + fade] * np.cos(t)
    return out


def write_ogg(path, samples, rate=48000):
    peak = float(np.abs(samples).max()) or 1.0
    data = (samples / max(peak, 1.0) * 32767 * (0.95 if peak >= 0.95 else 1.0)).astype('<i2')
    ogg.pcm_to_ogg(path, data.tobytes(), rate, channels=2, quality=ogg.MUSIC_BITRATE)
    rms = float(np.sqrt(np.mean((data / 32768.0) ** 2)))
    print(f'{path}\n  {len(data) / rate:.1f} s, peak {np.abs(data).max() / 32768:.2f}, loudness (RMS) {rms:.3f}, {path.stat().st_size / 1e6:.2f} MB')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--tags', required=True, help='what it sounds like: style, instruments, mood. Add "instrumental" for no singing')
    p.add_argument('--name', default='music')
    p.add_argument('--seconds', type=float, default=30.0)
    p.add_argument('--bpm', type=int, default=120)
    p.add_argument('--key', default='A minor', help='e.g. "C major", "F# minor"')
    p.add_argument('--beats', default='4', choices=['2', '3', '4', '6'], help='beats in a bar')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--steps', type=int, default=8, help='8: the turbo model\'s setting')
    p.add_argument('--lyrics', default='[Instrumental]', help='words to sing, or [Instrumental]')
    p.add_argument('--no-loop', action='store_true')
    p.add_argument('--temperature', type=float, default=0.85)
    p.add_argument('--top-p', type=float, default=0.9)
    p.add_argument('--no-audio-codes', action='store_true', help='skip the step that plans the music first: faster, simpler music')
    args = p.parse_args()

    if not (COMFY / 'models' / 'checkpoints' / CHECKPOINT).exists():
        sys.exit(f'missing the model: {COMFY / "models" / "checkpoints" / CHECKPOINT}\nsee docs/engine/22_AUDIO_GENERATION.md')
    OUT.mkdir(parents=True, exist_ok=True)
    made_server = None if answering() else start_server()
    try:
        seconds = args.seconds + (0.0 if args.no_loop else EXTRA_S)   # (extra: the model tends to end before the time it is given)
        info = run_graph(graph(args, args.seed, seconds))
        flac = OUT / f'_{args.name}_{args.seed}.flac'
        fetch(info, flac)
    finally:
        if made_server:
            made_server.terminate()
    npy = OUT / f'_{args.name}_{args.seed}.npy'
    samples = decode(flac, npy)
    if not np.isfinite(samples).all() or float(np.diff(samples, axis=0).std()) < 1e-4:   # (older ComfyUI reported success even then)
        sys.exit('the model made silence or bad numbers, not music: nothing written (docs/engine/22_AUDIO_GENERATION.md)')
    samples = trim_silence(samples)
    if not args.no_loop:
        samples = make_loop(samples, args.seconds)
        if len(samples) < int(args.seconds * 48000) - 48000:
            print(f'note: the model stopped early, so the loop is {len(samples) / 48000:.1f} s; try another --seed')
    write_ogg(OUT / f'{args.name}_{args.seed}.ogg', samples)
    flac.unlink(); npy.unlink()


if __name__ == '__main__':
    main()
