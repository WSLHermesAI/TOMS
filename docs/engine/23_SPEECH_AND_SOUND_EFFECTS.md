# 23. Speech and Sound Effects with ComfyLive

中文：[zh_TW/23_SPEECH_AND_SOUND_EFFECTS.md](zh_TW/23_SPEECH_AND_SOUND_EFFECTS.md).

ComfyLive (`AIGamestyle/ComfyLive`) makes two kinds of game audio with local AI models. Nothing is uploaded:

- **Speech:** a person says the sentences you give it, in the language you choose. It can be one line or a whole
  speech of several sentences. The model is Qwen3-TTS.
- **Sound effects:** animal sounds, background noise and metal hits, from a short description. The model is Stable
  Audio Open 1.0.

Each result is an MP3 with a `.json` settings file beside it. Open that `.json` again to make the same sound again.
Music (ACE-Step) and the maths-made effects are in [22_AUDIO_GENERATION.md](22_AUDIO_GENERATION.md). The tool's full
reference is [AIGamestyle/ComfyLive/README.md](../../AIGamestyle/ComfyLive/README.md).

## 1. The quickest way: a recipe file

A **recipe** is a small JSON file that describes one sound. There are 22 ready to try in
`AIGamestyle/ComfyLive/examples/`:

| Kind | Files |
|---|---|
| One sentence, a preset voice | `speech_en_gate_open`, `speech_zh_welcome`, `speech_ja_battle_cry`, `speech_en_shopkeeper` |
| A speech (several sentences) | `speech_en_king_speech`, `speech_zh_story_intro` |
| A voice described in words | `voice_en_old_wizard`, `voice_zh_fairy_girl` |
| Animals | `sfx_dog_bark`, `sfx_wolf_howl`, `sfx_birds_morning`, `sfx_frog_croak`, `sfx_monster_growl` |
| Background | `sfx_bg_forest`, `sfx_bg_dungeon`, `sfx_bg_rain` |
| Metal and battle | `sfx_sword_clang`, `sfx_swords_clash`, `sfx_armor_footsteps`, `sfx_iron_door`, `sfx_coins`, `sfx_magic_whoosh` |

**In the window:** start `ComfyLive.exe`, click **Open graph...**, pick a recipe, then click **Generate**. The sound
plays when it is done. **Save audio...** copies the MP3 and its settings `.json` wherever you want.

**From a command line** (no window; this is how an AI agent or a script uses it):

```
cd AIGamestyle\ComfyLive\build\Release
ComfyLive.exe --make=..\..\examples\speech_en_gate_open.json --out=D:\out\gate_open.mp3
```

This writes `gate_open.mp3` and `gate_open.json`. The exit code is 0 when the file was written. ComfyLive starts the
ComfyUI it needs if none is running, and leaves it running for the next command.

## 2. Writing your own recipe

### One sentence, or a speech

```json
{
    "graph": "speech",
    "text": "The gate of the tower is open. Climb, young hero, and do not look back.",
    "language": "English",
    "speaker": "Ryan",
    "style": "calm, slightly tired",
    "seed": 101
}
```

- `text` is what is said. For a speech, give a list: the sentences are joined and read as one speech, with natural
  pauses at the full stops.

  ```json
  "text": ["People of the valley, hear me.", "The tower has woken.", "The hero is already climbing."]
  ```
- `language`: `Chinese`, `English`, `Japanese`, `Korean`, `German`, `French`, `Russian`, `Portuguese`, `Spanish`,
  `Italian` or `Auto`. Write the text in that language. Naming the language gives a better accent than `Auto`, above
  all for short lines.
- `speaker`, one of 9 voices. Each speaks every language, but its own language best:

  | Speaker | Voice |
  |---|---|
  | `Ryan`, `Aiden` | English men |
  | `Vivian`, `Serena` | Chinese women |
  | `Uncle_fu` | an older Chinese man |
  | `Dylan` | a Beijing man |
  | `Eric` | a Sichuan man |
  | `Ono_anna` | a Japanese woman |
  | `Sohee` | a Korean woman |
- `style` (optional) says how to say it: `"angry, shouting"`, `"whispering, scared"`, `"a cheerful merchant, speaking
  fast"`.
- `seed` (optional): the same seed with the same values gives the same reading. Leave it out to get a new reading
  each time.

### A voice described in words

When none of the 9 speakers fits a character, use `"graph": "voice"` and describe the voice:

```json
{
    "graph": "voice",
    "text": "So, another climber. Many have come before you.",
    "language": "English",
    "voice": "An old male wizard, deep and slightly raspy voice, speaking slowly, calm and a little mysterious."
}
```

Describe who it is (age, man or woman, character), the timbre (deep, bright, raspy), the pace and the mood. Keep the
`seed` once you like a voice: a new seed can change the voice a little, too.

### A sound effect

```json
{
    "graph": "sfx",
    "prompt": "a steel sword striking a metal shield, sharp metallic clang with a short ring, single hit, close",
    "seconds": 1.5,
    "avoid": "music, speech, background noise, low quality"
}
```

- `prompt`: name the **source**, the **action**, the **material** and the **recording**: `"a big toad croaking three
  times, deep and wet, close, swamp"`. The model follows the source and the material best. Exact counts ("three
  times") are hit and miss.
- `seconds`: 1 to 3 for a hit, a bark or a click; 10 to 20 for a background loop. The most is 47.
- `avoid` (optional): what you don't want to hear. Without it, the default avoids music, speech and noise.
- Make a few takes (no `seed`) and keep the best one. Then copy the seed from its settings `.json` to remake it.

## 3. For an AI agent: headless mode

`ComfyLive.exe --make=<recipe>` is the simplest way: one command makes one file. To make many files, or to change
values without writing files, run the server once:

```
ComfyLive.exe --headless --port=8766
```

Then send one JSON command per line over TCP to `127.0.0.1:8766`, and read events (one JSON object per line) until the
`job_end` event:

```json
{"command": "make", "args": {"path": "D:/recipes/sfx_wolf_howl.json", "out": "D:/out/wolf.mp3"}}
{"command": "reset-graph", "args": {"graph": "speech"}}
{"command": "generate", "args": {"params": {"text": "Halt! Who goes there?", "speaker": "Aiden", "language": "English"}, "out": "D:/out/guard.mp3"}}
{"command": "comfyui-stop", "args": {"which": "all"}}
{"command": "shutdown"}
```

- `job_end` has `code` (0 ok, 2 bad arguments, 4 no ComfyUI, 5 ComfyUI refused the graph, 7 ComfyUI failed) and a
  `message`.
- The `audio` event has `path`, the file written, and `settings`, its `.json`.
- `get-params` lists every value of the current graph, with its choices and ranges.
- All the commands are in the README's "Headless mode" section.

## 4. ComfyUI: started once, stopped only when you say so

Starting ComfyUI takes from 15 seconds to a minute, so **ComfyLive never stops it by itself**. It keeps running
between runs, and the next sound starts at once. There are two ComfyUIs:

| | Port | Runs | Python |
|---|---|---|---|
| Main | 8188 | pictures, music, sound effects | `_comfyui/ComfyUI/.venv` (transformers 5.8) |
| Speech | 8189 | speech only (Qwen3-TTS) | `_comfyui/ComfyUI/.venv-speech` (transformers 4.57.3) |

ComfyLive sends each graph to the right one, and starts it when needed. To stop them:

- the **Stop ComfyUI** button in the window (stops both);
- `ComfyLive.exe --comfy-stop` (both), `--comfy-stop=main` or `--comfy-stop=speech`;
- the headless command `comfyui-stop`.

The window option **Stop the ComfyUI I started when closing** (off by default) stops them when the window closes.
`--stop-comfy-on-exit` does the same for `--make` and `--headless`.

**The GPU (12 GB) is shared.** Before each run, ComfyLive asks the other ComfyUI to unload its models, and the speech
model leaves the GPU after each run. So the two never fill the card together. Still, don't run a picture and a speech
at the same moment.

**Why two.** The speech node is made for transformers 4.57. In the main venv (5.8) it talks nonsense: on 2026-11-15
Whisper heard "Thank you very much for watching" for the line about the tower gate. Downgrading the main venv could
break the other nodes, so speech has a venv of its own.

## 5. Put a sound in the game

The game uses OGG Vorbis only ([22_AUDIO_GENERATION.md](22_AUDIO_GENERATION.md) §3). Convert the file, then add it by
key to `assets/media/resources.json` (`sfx.*`):

```
tools\ffmpeg\ffmpeg.exe -i D:\out\wolf.mp3 -c:a libvorbis -q:a 5 assets\media\sfx\wolf_howl.ogg
python tools/make_res_keys.py
```

A sound that comes out too quiet (soft backgrounds often do) gets its level raised on the way:
`tools\ffmpeg\ffmpeg.exe -i in.mp3 -af loudnorm=I=-23 -c:a libvorbis -q:a 5 out.ogg`.

A sound a monster makes goes through `f_PlayFoeSound(foe, key)`, so it stops or fades when the monster dies (the
project rule "Battle effects belong to their monster").

## 6. Check what was made

Listen to every file before it goes into the game. For speech, a quick check that the right words are spoken: Whisper
(speech to text) in the speech venv. It does not judge quality: listen for pronunciation, repeats and naturalness
yourself. On 2026-11-15 it heard the English and the wizard lines exactly, and the Chinese story with only soundalike
characters different. For a short Japanese line it gave 「行くぞ ハケルモンか」 for 「行くぞ！負けるもんか！」, which
may be Whisper's mistake rather than the speech's. Listen to that one.

## 7. First setup on a new PC

| What | Where | Size |
|---|---|---|
| ComfyUI-Qwen-TTS node | `_comfyui/ComfyUI/custom_nodes/ComfyUI-Qwen-TTS` (`git clone https://github.com/flybirdxx/ComfyUI-Qwen-TTS`) | small |
| Qwen3-TTS models | `D:\Work\AI\models\qwen-tts\` (`Qwen3-TTS-Tokenizer-12Hz`, `Qwen3-TTS-12Hz-1.7B-CustomVoice`, `Qwen3-TTS-12Hz-1.7B-VoiceDesign`), linked from `_comfyui/ComfyUI/models/qwen-tts` | 9.3 GB |
| Speech venv | `_comfyui/ComfyUI/.venv-speech`: Python 3.11, torch 2.14.1 cu130, ComfyUI's requirements (with `torchsde`), the node's requirements, `onnxruntime`, `transformers==4.57.3` | about 6 GB |
| Stable Audio Open 1.0 | `D:\Work\AI\models\checkpoints\stable-audio-open-1.0.safetensors`: Hugging Face's `model.safetensors`, **renamed**; gated, accept the licence first (§7.1) | about 5 GB |
| T5 encoder | `D:\Work\AI\models\text_encoders\t5-base.safetensors` | 850 MB |

The exact commands are in the ComfyLive README's "Speech" and "Sound effects" sections.

### 7.1 Getting Stable Audio Open (account, licence, download)

Stable Audio Open is free, but Stability AI wants each user to accept its licence once, with a free Hugging Face
account. Without that, the download is refused (HTTP 401), and ComfyLive's sound graph is refused with
`Value not in list ckpt_name: 'stable-audio-open-1.0.safetensors'`.

1. **Account.** Go to <https://huggingface.co/join>, sign up with an email address, and confirm it from the email you
   get.
2. **Licence.** While signed in, open <https://huggingface.co/stabilityai/stable-audio-open-1.0>, fill in the short
   form at the top, and click **Agree and access repository**. Afterwards, the **Files and versions** tab lists
   `model.safetensors`.
3. **Download, either way:**
   - **In the browser (simplest):** on <https://huggingface.co/stabilityai/stable-audio-open-1.0/tree/main>, click the
     download arrow on the `model.safetensors` row (about 4.9 GB). While it downloads, the browser names it
     `Unconfirmed NNN.crdownload` (Chrome/Edge); leave it alone until it is done.
   - **With a token:** at <https://huggingface.co/settings/tokens> click **Create new token**, type **Read**, then copy
     it (`hf_...`; keep it private). In a terminal run `hf auth login` (on the owner's PC the full path is
     `C:\Users\leeyo\AppData\Local\Packages\PythonSoftwareFoundation.Python.3.11_qbz5n2kfra8p0\LocalCache\local-packages\Python311\Scripts\hf.exe`),
     paste the token (nothing shows while pasting; in some windows right-click pastes, Ctrl+V does not), answer
     **n** to "git credential", and wait for **Login successful**. Then:
     `hf download stabilityai/stable-audio-open-1.0 model.safetensors --local-dir D:\Work\AI\models\checkpoints\sao`.
4. **Name and place.** It is one **file**, not a folder. Rename `model.safetensors` to
   **`stable-audio-open-1.0.safetensors`** (with the final **s**) and put it straight in `D:\Work\AI\models\checkpoints\`.
   Why: ComfyUI loads a model only by its exact file name, and ComfyLive's sound graph, the example recipes and every
   saved settings `.json` ask for `stable-audio-open-1.0.safetensors`. Also, `model.safetensors` is the name most models
   use, so a second download could overwrite it. A complete file is 4,853,889,016 bytes.
5. **Use it.** In ComfyLive press **Reconnect** (or restart the main ComfyUI) so ComfyUI sees the new file, then open
   `examples\sfx_dog_bark.json` and click **Generate**.

**Status (2026-11-15):** both work. Speech was checked as in §6. All 14 sound examples were made, 2 to 5 s each, at
their asked lengths. The forest and morning-bird backgrounds come out very quiet (peak about −25 dB): raise them when
converting (`-af loudnorm=I=-23`, §5).

## 8. Licences

- **Qwen3-TTS:** the node is Apache-2.0, and the models are under the Qwen3-TTS licence. Read it before shipping voices
  in the game.
- **Stable Audio Open 1.0:** the Stability AI Community License, free under US$1M yearly revenue.
- Don't ask either model to imitate a real person's voice.
