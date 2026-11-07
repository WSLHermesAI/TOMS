"""ogg.py -- writes OGG Vorbis, the format of all the game's sound and music (docs/engine/22_AUDIO_GENERATION.md), with ffmpeg.

ffmpeg is looked for in: the environment variable TOMS_FFMPEG, then tools/ffmpeg (tools/get_ffmpeg.cmd puts it there), then PATH.
"""
import os
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

TOOLS_COPY = Path(__file__).resolve().parents[1] / 'ffmpeg' / 'ffmpeg.exe'
SFX_QUALITY = ['-q:a', '5']     # Vorbis's quality scale; short sounds need no fixed bitrate
MUSIC_BITRATE = ['-b:a', '160k']


def find_ffmpeg():
    for candidate in [os.environ.get('TOMS_FFMPEG'), TOOLS_COPY, shutil.which('ffmpeg')]:
        if candidate and Path(candidate).exists():
            return candidate
    sys.exit('ffmpeg not found: run tools\\get_ffmpeg.cmd to download it into the project (docs/engine/22_AUDIO_GENERATION.md)')


def wav_to_ogg(wav_path, ogg_path, quality=SFX_QUALITY):
    command = [find_ffmpeg(), '-nostdin', '-y', '-hide_banner', '-loglevel', 'error', '-i', str(wav_path), '-c:a', 'libvorbis', *quality, str(ogg_path)]
    result = subprocess.run(command, capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    if result.returncode != 0:
        sys.exit(f'ffmpeg failed on {wav_path}: {result.stderr.strip()[:400]}')


def pcm_to_ogg(ogg_path, pcm_bytes, rate, channels=1, quality=SFX_QUALITY):
    """16-bit samples to an OGG, through a WAV that is thrown away."""
    with tempfile.TemporaryDirectory() as folder:
        temporary = Path(folder) / 'sound.wav'
        with wave.open(str(temporary), 'wb') as w:
            w.setnchannels(channels); w.setsampwidth(2); w.setframerate(rate)
            w.writeframes(pcm_bytes)
        wav_to_ogg(temporary, ogg_path, quality)
