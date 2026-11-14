# 22. Making Music and Sound Effects

中文：[zh_TW/22_AUDIO_GENERATION.md](zh_TW/22_AUDIO_GENERATION.md).

**All the game's sound and music is OGG Vorbis** (the owner, 2026-11-07). Two local tools make audio for the game. Neither uploads anything, and neither changes the game: the files go to `Build/art_out/audio/` until someone copies them in.

| | Music | Sound effects |
|---|---|---|
| Tool | `tools/audio/make_music.py` | `tools/audio/make_sfx.py` |
| How | ACE-Step 1.5, an AI model, in the local ComfyUI | Plain maths from short recipes, no AI |
| Time | About 40 s for 30 to 60 s, including starting ComfyUI | Under a second |
| Output | 48 kHz stereo OGG, 160 kbps (about 0.5 MB for 26 s) | 44.1 kHz mono OGG, quality 5, the same as `assets/media/sfx/` |

## 1. Make music

```
python tools/audio/make_music.py --name forest_battle --seconds 30 --bpm 140 --seed 2 --tags "fantasy RPG battle music, instrumental, full orchestra from the first second, taiko drums, fast strings, wooden flute, bright, energetic, constant intensity, no intro, no ending, game loop"
```

This writes `Build/art_out/audio/music/forest_battle_2.ogg`. The script starts ComfyUI hidden and stops it afterwards, if it is not running already.

| Option | Meaning |
|---|---|
| `--tags` | What it should sound like: style, instruments, mood |
| `--seconds` | The length you want; the model may stop a little sooner |
| `--bpm`, `--key`, `--beats` | Tempo, key such as `"F# minor"`, beats in a bar |
| `--seed` | Another take of the same words. Try several and keep the best |
| `--name` | The file's name |
| `--no-loop` | Keep the piece as made, with no crossfade from the end into the start |
| `--lyrics` | Words to sing; the default is `[Instrumental]` |

**Getting a good loop.** Without help the model writes a song, with a quiet intro and an ending. Say `constant intensity, no intro, no ending, game loop` in the tags. The script cuts silence from both ends and crossfades the end into the start over 1.5 s. Listen to the loop point: the crossfade removes a click, but it cannot make two different musical moments match.

## 2. Make sound effects

```
python tools/audio/make_sfx.py --list
python tools/audio/make_sfx.py hit_heavy coin
python tools/audio/make_sfx.py --all
```

This writes `Build/art_out/audio/sfx/<name>.ogg`. The recipes are `sword_slash`, `bow_release`, `hit_heavy`, `dodge`, `danger_warning`, `coin`, `ui_click` and `level_up`.

A new sound is a new entry in `tools/audio/sfx_recipes.json`, a list of layers played together:

```json
"my_zap": [
  { "wave": "saw", "ms": 200, "curve": 2, "gain": 0.6, "pitch": [1800, 200], "low": [6000, 1200] },
  { "wave": "noise", "ms": 80, "curve": 3, "gain": 0.3, "high": 2000 }
]
```

| Field | Meaning |
|---|---|
| `wave` | `noise` (hiss, impacts, whooshes), `sine` (soft), `square` (retro), `saw` (buzzy) |
| `ms`, `at` | How long it lasts; when it starts |
| `gain` | How loud this layer is against the others |
| `attack`, `curve` | Milliseconds to full volume; how fast it fades (1 even, higher faster) |
| `pitch` | `[from, to]` in Hz: falling sounds heavy, rising sounds bright |
| `low`, `high` | Keep what is below or above this Hz; `low` may be `[from, to]`, which makes a whoosh |

`--seed` gives another variation of a sound with noise in it.

## 3. Put one in the game

1. Listen to it.
2. Copy the OGG to `assets/media/sfx/` (`make_sfx.py <name> --to-game` writes it there at the game's level) or `assets/media/music/`.
3. For a new file, add its key to `assets/media/resources.json`, such as `"sfx.coin": { "type": "file", "path": "sfx/coin.ogg" }`, and run `python tools/make_res_keys.py`.
4. Name it by its key: in data, `"sound": "sfx.coin"`; in code, `m_oAudio.f_Play(toms::key::g_pszSfxCoin)`. No file name goes in code ([12_RESOURCE_MANAGER.md](12_RESOURCE_MANAGER.md) §4.3b).

Everything plays through the `Audio` engine, so it stays muted in the background.

**The game's music** (the owner, 2026-11-14): five looping tracks, all set in `battle.json` `music`:

| Track | Key | Loop | Plays |
|---|---|---|---|
| Walking on a stage: light, nervous | `music.stage_walk` | 75 s | `"stage"`: on a stage with no battle (`"stageVolume"`, `"stageFadeInMs"`); battles and the title replace it |
| A normal battle | `music.battle_normal` | 31 s | `"normal"`, or a floor range's own (`backdrops` `"music"`) |
| A small boss | `music.boss_mini` | 52 s | `"boss"` `"mini"`: `enemies.json` `"bossTier": "mini"` (the stone golem, the tutorial toad) |
| A stage boss | `music.boss_stage` | 54 s | `"boss"` `"stage"`: the F01-F10 stair guards |
| The final boss | `music.boss_final` | 56 s | `"boss"` `"final"`: Vorkath |

A battle takes the highest boss tier among its monsters, else the floor's track, else `"normal"`. `Audio::f_PlayMusic(key, volume, fadeInMs)` loops it and fades it over the track before, and the stage track comes back when the fight is over. The tracks were made with `make_music.py` (2-6 takes each; in the game: stage_walk take 1, battle_normal take 4, boss_mini take 3, boss_stage take 1, boss_final take 1; every take is in `Build/art_out/audio/music/`). `music.forest_battle` stays in the manifest, unused. **Size** (the owner, 2026-11-14: about 30 % of the made size): every track in the game is re-encoded at 24 kHz, Vorbis quality 1 (about 0.2-0.4 MB each, 1.6 MB in all), e.g. `tools/audio/mp3_to_ogg.py` with a target rate, or ffmpeg `-ar 24000 -c:a libvorbis -q:a 1`; `make_music.py` still writes 48 kHz, 160 kbps, so a new track is shrunk the same way when it goes in.

The `unit.audio_ogg_test` test checks every `sfx.*` and `music.*` key decodes the way the game plays it, every file in those folders has a key, every sound the data names is a key, and music keeps playing past its end. The game itself only prints `[audio] cannot play` for a bad sound.

**A sound bound to its owner:** `Audio::f_PlayOwned(key)` plays a sound and returns its id; `f_StopOwned(id, fadeMs)` stops it early (with a fade) and `f_OwnedPlaying(id)` says if it still plays. A monster's sounds go through it (`Game::f_PlayFoeSound`), so its death can stop, fade or keep them (CLAUDE.md "Battle effects belong to their monster").

**MP3s from elsewhere:** `tools\convert_mp3_to_ogg.cmd` converts every MP3 in a folder and all its sub-folders to an OGG beside it (`song.mp3` → `song.ogg`; the MP3 is kept, and an OGG newer than its MP3 is skipped). Double-click it to pick the folder, then the **target frequency** (keep, 44100, 32000, 22050, 16000 or 11025 Hz: lower is smaller), music or sound effects, and whether to convert again; a last window says how many were converted, which failed, and the MB before and after. From a console: `tools\convert_mp3_to_ogg.cmd <folder>` (music quality, 160 kbps), `--rate 22050` (the frequency), `--sfx` (sound-effect quality), `--force` (convert again), `--no-gui` (no windows; exit 1 on a failure). A frequency only goes down (a file already under it keeps its own), and with one, music is encoded by quality (Vorbis q 4) instead of 160 kbps: at a fixed bitrate a lower frequency does not make the file smaller (measured: 22050 Hz at 160 kbps was bigger than 44100 Hz) and 11025 Hz fails. A 20 s tone: 57 KB at 44100 Hz, 37 KB at 22050, 28 KB at 16000. A file whose OGG is not smaller than its MP3 is listed with both sizes and how much bigger (a short or already small MP3 can grow: 8.4 → 10.5 KB); the OGG is kept. A low-rate MP3 that cannot take 160 kbps is encoded by quality instead. It is `tools/audio/mp3_to_ogg.py`, using `ogg.py`'s `convert`.

**ffmpeg** writes the OGG files (`tools/audio/ogg.py`). **On a new PC, double-click `tools\get_ffmpeg.cmd` once:** it downloads [BtbN's ffmpeg 9.0 LGPL build](https://github.com/BtbN/FFmpeg-Builds/releases) (about 170 MB) into `tools\ffmpeg\`, which is not in git, and checks it against the SHA-256 list in the same release. The build is refreshed daily, so two PCs can get slightly different builds of 9.0. The tools look at `TOMS_FFMPEG`, then `tools\ffmpeg\`, then PATH. It is for making sounds only and does not ship with the game. The game does not need it: it decodes OGG with `stb_vorbis` (`src/third_party/stb/stb_vorbis.c`, v1.22, public domain).

## 4. Licences

- **ACE-Step 1.5:** the [official licence file](https://raw.githubusercontent.com/ace-step/ACE-Step-1.5/main/LICENSE) is MIT (checked 2026-11-07), which allows commercial use. Check it again before a release.
- **Stable Audio Open and TangoFlux** are non-commercial without a licence from Stability AI, so they are not used here.
- **The recipes** use no model and no outside samples.
- **stb_vorbis** is public domain or MIT, as its user chooses.

## 5. First setup, once

For sound effects and music, run `tools\get_ffmpeg.cmd`. For music, also:

Download `ace_step_1.5_turbo_aio.safetensors` (10 GB) from [Comfy-Org/ace_step_1.5_ComfyUI_files](https://huggingface.co/Comfy-Org/ace_step_1.5_ComfyUI_files/tree/main) into `AIGamestyle/_comfyui/ComfyUI/models/checkpoints/`. That folder is not in git.

## 6. What went wrong here, and how it was fixed

On ComfyUI 0.21.1 with PyTorch 2.6 (RTX 4070), ACE-Step made bad numbers (NaN) in its default 16-bit format for anything longer than a short clip: 10 s worked, 31 s and 60 s did not. With the planning step on, ComfyUI **crashed**. With it off, ComfyUI **reported success and saved a flat signal with no sound**. Forcing 32-bit (`--force-fp32`) worked, but took about 3 minutes.

**Updating fixed it** (2026-11-07):

| | Before | Now |
|---|---|---|
| ComfyUI | 0.21.1 | **0.44.2** |
| PyTorch | 2.6.0 (CUDA 12.4) | **2.14.1 (CUDA 13.0)**, torchvision 0.29.1, torchaudio 2.11.0 |
| 30 s of music, default settings | Crash | **13 s** |
| 60 s of music, default settings | Crash | **16 s** |

ComfyUI 0.44 needs PyTorch 2.7 or newer: with 2.6 it does not start. `--force-fp32` is no longer used.

**What the script still does:**

- It starts ComfyUI with `--disable-all-custom-nodes`, because music needs none of the art add-ons and they start slowly. A ComfyUI that is already running also works, with its add-ons.
- It refuses to write a silent result, in case this ever comes back.

**Checked on the art side:** every add-on loads, and an SDXL image with `comfyui-inspyrenet-rembg` background removal generates correctly. **Not run yet:** the full art workflows of [20_CHARACTER_ART.md](20_CHARACTER_ART.md), with ControlNet and IPAdapter; Hunyuan3D and Florence2 generation. `comfyui-rmbg` prints `No module named 'triton'` for its SAM3 node, as it did before the update.

**To go back:** in `AIGamestyle/_comfyui/ComfyUI`, run `git checkout v0.21.1`, then `.venv\Scripts\python -m pip install -r requirements.txt` and `.venv\Scripts\python -m pip install torch==2.6.0 torchvision==0.21.0 torchaudio==2.6.0 --index-url https://download.pytorch.org/whl/cu124`. Music then needs `--force-fp32` again.
