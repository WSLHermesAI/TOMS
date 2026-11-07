> 英文原文：[22_AUDIO_GENERATION.md](../22_AUDIO_GENERATION.md)

# 22. 製作音樂與音效

**遊戲的所有音效與音樂都使用 OGG Vorbis**（擁有者，2026-11-07）。兩個本機工具為遊戲製作音訊。它們不會上傳任何東西，也不會改動遊戲：檔案會放在 `Build/art_out/audio/`，直到有人把它們複製進去。

| | 音樂 | 音效 |
|---|---|---|
| 工具 | `tools/audio/make_music.py` | `tools/audio/make_sfx.py` |
| 方式 | ACE-Step 1.5 AI 模型，在本機 ComfyUI 中執行 | 由簡短配方以數學產生，不用 AI |
| 時間 | 30 到 60 秒的音樂約 40 秒（含啟動 ComfyUI） | 不到 1 秒 |
| 輸出 | 48 kHz 立體聲 OGG，160 kbps（26 秒約 0.5 MB） | 44.1 kHz 單聲道 OGG，品質 5，與 `assets/media/sfx/` 相同 |

## 1. 製作音樂

```
python tools/audio/make_music.py --name forest_battle --seconds 30 --bpm 140 --seed 2 --tags "fantasy RPG battle music, instrumental, full orchestra from the first second, taiko drums, fast strings, wooden flute, bright, energetic, constant intensity, no intro, no ending, game loop"
```

這會寫出 `Build/art_out/audio/music/forest_battle_2.ogg`。如果 ComfyUI 沒有在執行，腳本會隱藏啟動它，完成後再關閉。

| 選項 | 意思 |
|---|---|
| `--tags` | 想要的聲音：風格、樂器、情緒 |
| `--seconds` | 想要的長度；模型可能提早一點結束 |
| `--bpm`、`--key`、`--beats` | 速度、調性（例如 `"F# minor"`）、每小節拍數 |
| `--seed` | 同樣文字的另一個版本。多試幾個，留下最好的 |
| `--name` | 檔案名稱 |
| `--no-loop` | 保留原樣，不把結尾交叉淡入開頭 |
| `--lyrics` | 要唱的歌詞；預設是 `[Instrumental]` |

**做出好的循環：** 如果不提示，模型會寫成一首歌，有安靜的前奏和結尾。請在 tags 中加上 `constant intensity, no intro, no ending, game loop`。腳本會切掉兩端的靜音，並用 1.5 秒把結尾交叉淡入開頭。請聽一下循環點：交叉淡入能消除爆音，但無法讓兩段不同的音樂完全接上。

## 2. 製作音效

```
python tools/audio/make_sfx.py --list
python tools/audio/make_sfx.py hit_heavy coin
python tools/audio/make_sfx.py --all
```

這會寫出 `Build/art_out/audio/sfx/<名稱>.ogg`。目前的配方有 `sword_slash`、`bow_release`、`hit_heavy`、`dodge`、`danger_warning`、`coin`、`ui_click` 與 `level_up`。

新音效就是在 `tools/audio/sfx_recipes.json` 加一個項目，內容是一起播放的多個圖層：

```json
"my_zap": [
  { "wave": "saw", "ms": 200, "curve": 2, "gain": 0.6, "pitch": [1800, 200], "low": [6000, 1200] },
  { "wave": "noise", "ms": 80, "curve": 3, "gain": 0.3, "high": 2000 }
]
```

| 欄位 | 意思 |
|---|---|
| `wave` | `noise`（嘶聲、撞擊、揮動）、`sine`（柔和）、`square`（復古）、`saw`（嗡嗡聲） |
| `ms`、`at` | 持續多久；何時開始 |
| `gain` | 這個圖層相對其他圖層的音量 |
| `attack`、`curve` | 到達最大音量的毫秒數；淡出速度（1 為平均，越高越快） |
| `pitch` | `[起, 迄]`，單位 Hz：下降聽起來沉重，上升聽起來明亮 |
| `low`、`high` | 保留低於或高於此 Hz 的聲音；`low` 可以是 `[起, 迄]`，能做出揮動聲 |

`--seed` 可以讓含有 noise 的音效產生另一種變化。

## 3. 放進遊戲

1. 先聽過。
2. 把 OGG 複製到 `assets/media/sfx/`（`make_sfx.py <名稱> --to-game` 會以遊戲的音量寫到那裡）或 `assets/media/music/`。
3. 如果是新檔案，把它的 key 加進 `assets/media/resources.json`，例如 `"sfx.coin": { "type": "file", "path": "sfx/coin.ogg" }`，並執行 `python tools/make_res_keys.py`。
4. 用 key 指名：資料中寫 `"sound": "sfx.coin"`；程式碼中寫 `m_oAudio.f_Play(toms::key::g_pszSfxCoin)`。程式碼中不寫檔名（[12_RESOURCE_MANAGER.md](12_RESOURCE_MANAGER.md) §4.3b）。

所有聲音都經由 `Audio` 引擎播放，所以在背景時會保持靜音。

**戰鬥音樂：** `battle.json` 的 `backdrops` 指定某個樓層範圍的曲目（`"music": "music.forest_battle"`），`battle.json` 的 `music` 設定 `volume`、`fadeInMs` 與 `fadeOutMs`。`Audio::f_PlayMusic(key, volume, fadeInMs)` 會循環播放，戰鬥結束時淡出。目前只有戰鬥有音樂：標題、地圖與選單都沒有。

`unit.audio_ogg_test` 測試會檢查每個 `sfx.*` 與 `music.*` key 都能以遊戲播放的方式解碼、這些資料夾中的每個檔案都有 key、資料指名的每個音效都是 key，以及音樂播到結尾後仍繼續播放。遊戲本身遇到有問題的音效時只會印出 `[audio] cannot play`。

**ffmpeg** 用來寫出 OGG 檔案（`tools/audio/ogg.py`）。**在新電腦上，請雙擊一次 `tools\get_ffmpeg.cmd`：** 它會把 [BtbN 的 ffmpeg 9.0 LGPL 版](https://github.com/BtbN/FFmpeg-Builds/releases)（約 170 MB）下載到不在 git 中的 `tools\ffmpeg\`，並用同一個發行版中的 SHA-256 清單檢查。這個版本每天更新，所以兩台電腦可能拿到 9.0 略有不同的建置。工具會依序尋找 `TOMS_FFMPEG`、`tools\ffmpeg\`、PATH。它只用來製作音效，不會隨遊戲發行。遊戲本身不需要它：遊戲用 `stb_vorbis`（`src/third_party/stb/stb_vorbis.c`，v1.22，公有領域）解碼 OGG。

## 4. 授權

- **ACE-Step 1.5：** [官方授權檔](https://raw.githubusercontent.com/ace-step/ACE-Step-1.5/main/LICENSE)是 MIT（2026-11-07 檢查），允許商業使用。發行前請再檢查一次。
- **Stable Audio Open 與 TangoFlux** 沒有 Stability AI 的授權就不能商業使用，所以這裡不使用。
- **音效配方** 不使用模型，也不使用外部取樣。
- **stb_vorbis** 可由使用者選擇公有領域或 MIT。

## 5. 第一次設定（只需一次）

音效與音樂都需要先執行 `tools\get_ffmpeg.cmd`。音樂還需要：

從 [Comfy-Org/ace_step_1.5_ComfyUI_files](https://huggingface.co/Comfy-Org/ace_step_1.5_ComfyUI_files/tree/main) 下載 `ace_step_1.5_turbo_aio.safetensors`（10 GB）到 `AIGamestyle/_comfyui/ComfyUI/models/checkpoints/`。該資料夾不在 git 中。

## 6. 這裡遇到的問題，以及如何修好

在 ComfyUI 0.21.1 與 PyTorch 2.6（RTX 4070）上，ACE-Step 預設的 16 位元格式只要長度超過短片段就會算出錯誤的數值（NaN）：10 秒正常，31 秒與 60 秒失敗。開啟規劃步驟時，ComfyUI 會**當掉**；關閉時，ComfyUI 會**回報成功，卻存下沒有聲音的平坦訊號**。強制 32 位元（`--force-fp32`）可以運作，但需要約 3 分鐘。

**更新後修好了**（2026-11-07）：

| | 之前 | 現在 |
|---|---|---|
| ComfyUI | 0.21.1 | **0.44.2** |
| PyTorch | 2.6.0（CUDA 12.4） | **2.14.1（CUDA 13.0）**、torchvision 0.29.1、torchaudio 2.11.0 |
| 30 秒音樂，預設設定 | 當掉 | **13 秒** |
| 60 秒音樂，預設設定 | 當掉 | **16 秒** |

ComfyUI 0.44 需要 PyTorch 2.7 以上：使用 2.6 時無法啟動。現在不再使用 `--force-fp32`。

**腳本仍然會做的事：**

- 用 `--disable-all-custom-nodes` 啟動 ComfyUI，因為音樂不需要任何美術外掛，而且外掛啟動較慢。已經在執行的 ComfyUI（含外掛）也可以使用。
- 不會寫出沒有聲音的結果，以防問題再次出現。

**美術方面已檢查：** 所有外掛都能載入，SDXL 圖片加上 `comfyui-inspyrenet-rembg` 去背可以正確產生。**尚未執行：** [20_CHARACTER_ART.md](20_CHARACTER_ART.md) 的完整美術流程（ControlNet 與 IPAdapter）；Hunyuan3D 與 Florence2 的產生。`comfyui-rmbg` 的 SAM3 節點會顯示 `No module named 'triton'`，更新前也是如此。

**如何還原：** 在 `AIGamestyle/_comfyui/ComfyUI` 中執行 `git checkout v0.21.1`，然後執行 `.venv\Scripts\python -m pip install -r requirements.txt` 與 `.venv\Scripts\python -m pip install torch==2.6.0 torchvision==0.21.0 torchaudio==2.6.0 --index-url https://download.pytorch.org/whl/cu124`。之後音樂又會需要 `--force-fp32`。
