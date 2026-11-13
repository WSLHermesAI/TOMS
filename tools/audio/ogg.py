"""ogg.py -- writes OGG Vorbis, the format of all the game's sound and music (docs/engine/22_AUDIO_GENERATION.md), with ffmpeg.

ffmpeg is looked for in: the environment variable TOMS_FFMPEG, then tools/ffmpeg (tools/get_ffmpeg.cmd puts it there), then PATH.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

TOOLS_COPY = Path(__file__).resolve().parents[1] / 'ffmpeg' / 'ffmpeg.exe'
SFX_QUALITY = ['-q:a', '5']     # Vorbis's quality scale; short sounds need no fixed bitrate
MUSIC_BITRATE = ['-b:a', '160k']
MUSIC_QUALITY = ['-q:a', '4']   # music at a lower sample rate: a fixed bitrate would not shrink it (and fails at 11025 Hz)


def find_ffmpeg():
    for candidate in [os.environ.get('TOMS_FFMPEG'), TOOLS_COPY, shutil.which('ffmpeg')]:
        if candidate and Path(candidate).exists():
            return candidate
    sys.exit('ffmpeg not found: run tools\\get_ffmpeg.cmd to download it into the project (docs/engine/22_AUDIO_GENERATION.md)')


def sample_rate(path, ffmpeg=None):
    """The sound's sample rate in Hz (what ffmpeg reports), or None."""
    result = subprocess.run([ffmpeg or find_ffmpeg(), '-nostdin', '-hide_banner', '-i', str(path)], capture_output=True, text=True,
                            creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    found = re.search(r'Audio:.*?(\d+) Hz', result.stderr)
    return int(found.group(1)) if found else None


def convert(source_path, ogg_path, quality=SFX_QUALITY, ffmpeg=None, rate=None):
    """Any sound ffmpeg reads (WAV, MP3, FLAC, M4A...) to an OGG; rate: resampled to that many Hz (None: as it is).
    Returns None when done, else why it failed."""
    command = [ffmpeg or find_ffmpeg(), '-nostdin', '-y', '-hide_banner', '-loglevel', 'error', '-i', str(source_path),
               '-vn', '-c:a', 'libvorbis', *quality, *(['-ar', str(rate)] if rate else []), str(ogg_path)]   # -vn: no cover picture
    result = subprocess.run(command, capture_output=True, text=True, creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    return None if result.returncode == 0 else (result.stderr.strip()[-400:] or f'ffmpeg exit {result.returncode}')   # (its end says why)


def wav_to_ogg(wav_path, ogg_path, quality=SFX_QUALITY):
    why = convert(wav_path, ogg_path, quality)
    if why:
        sys.exit(f'ffmpeg failed on {wav_path}: {why}')


def pcm_to_ogg(ogg_path, pcm_bytes, rate, channels=1, quality=SFX_QUALITY):
    """16-bit samples to an OGG, through a WAV that is thrown away."""
    with tempfile.TemporaryDirectory() as folder:
        temporary = Path(folder) / 'sound.wav'
        with wave.open(str(temporary), 'wb') as w:
            w.setnchannels(channels); w.setsampwidth(2); w.setframerate(rate)
            w.writeframes(pcm_bytes)
        wav_to_ogg(temporary, ogg_path, quality)
