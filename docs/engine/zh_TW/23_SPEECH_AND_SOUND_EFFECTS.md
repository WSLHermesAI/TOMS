> 英文原文：[23_SPEECH_AND_SOUND_EFFECTS.md](../23_SPEECH_AND_SOUND_EFFECTS.md)

# 23. 用 ComfyLive 做語音和音效

ComfyLive（`AIGamestyle/ComfyLive`）用本機的 AI 模型做兩種遊戲音訊，不會上傳任何東西：

- **語音**：一個人用你選的語言說出你給的句子。可以是一句話，也可以是好幾句組成的一段演講。模型是 Qwen3-TTS。
- **音效**：動物叫聲、背景聲、金屬碰撞，用一小段描述生成。模型是 Stable Audio Open 1.0。

每個結果是一個 MP3，旁邊有一個同名的 `.json` 設定檔。再開啟那個 `.json`，就能做出同一個聲音。
音樂（ACE-Step）和用數學算出來的音效請看 [22_AUDIO_GENERATION.md](22_AUDIO_GENERATION.md)。工具的完整說明在
[AIGamestyle/ComfyLive/README.md](../../../AIGamestyle/ComfyLive/README.md)。

## 1. 最快的方法：配方檔

**配方**是一個描述一個聲音的小 JSON 檔。`AIGamestyle/ComfyLive/examples/` 裡有 22 個可以直接試：

| 種類 | 檔案 |
|---|---|
| 一句話，預設聲音 | `speech_en_gate_open`、`speech_zh_welcome`、`speech_ja_battle_cry`、`speech_en_shopkeeper` |
| 一段演講（好幾句） | `speech_en_king_speech`、`speech_zh_story_intro` |
| 用文字描述的聲音 | `voice_en_old_wizard`、`voice_zh_fairy_girl` |
| 動物 | `sfx_dog_bark`、`sfx_wolf_howl`、`sfx_birds_morning`、`sfx_frog_croak`、`sfx_monster_growl` |
| 背景 | `sfx_bg_forest`、`sfx_bg_dungeon`、`sfx_bg_rain` |
| 金屬和戰鬥 | `sfx_sword_clang`、`sfx_swords_clash`、`sfx_armor_footsteps`、`sfx_iron_door`、`sfx_coins`、`sfx_magic_whoosh` |

**在視窗裡：** 開啟 `ComfyLive.exe`，按「開啟流程…」，選一個配方，再按「生成」。完成後會自動播放。「儲存音訊…」會把 MP3
和它的設定 `.json` 一起複製到你要的地方。

**用命令列**（沒有視窗；AI 代理或腳本就是這樣用的）：

```
cd AIGamestyle\ComfyLive\build\Release
ComfyLive.exe --make=..\..\examples\speech_en_gate_open.json --out=D:\out\gate_open.mp3
```

這會寫出 `gate_open.mp3` 和 `gate_open.json`。檔案寫好時結束碼是 0。如果需要的 ComfyUI 沒在執行，ComfyLive 會自己啟動它，
之後讓它繼續執行，給下一個指令用。

## 2. 自己寫配方

### 一句話，或一段演講

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

- `text` 是要說的話。演講就給一個清單：句子會接起來當成一段話念出，句號處自然停頓。

  ```json
  "text": ["People of the valley, hear me.", "The tower has woken.", "The hero is already climbing."]
  ```
- `language`：`Chinese`、`English`、`Japanese`、`Korean`、`German`、`French`、`Russian`、`Portuguese`、`Spanish`、
  `Italian` 或 `Auto`。文字要用那個語言寫。指定語言的口音比 `Auto` 好，短句尤其如此。
- `speaker`，9 個聲音之一。每個都能說各種語言，但自己的母語最自然：

  | 聲音 | 說明 |
  |---|---|
  | `Ryan`、`Aiden` | 英語男聲 |
  | `Vivian`、`Serena` | 中文女聲 |
  | `Uncle_fu` | 中文年長男聲 |
  | `Dylan` | 北京男聲 |
  | `Eric` | 四川男聲 |
  | `Ono_anna` | 日語女聲 |
  | `Sohee` | 韓語女聲 |
- `style`（可不填）說怎麼說：`"angry, shouting"`、`"whispering, scared"`、`"a cheerful merchant, speaking fast"`。
- `seed`（可不填）：相同種子加相同數值，念法相同。不填的話，每次都是新的念法。

### 用文字描述聲音

9 個聲音都不適合某個角色時，用 `"graph": "voice"`，然後描述那個聲音：

```json
{
    "graph": "voice",
    "text": "So, another climber. Many have come before you.",
    "language": "English",
    "voice": "An old male wizard, deep and slightly raspy voice, speaking slowly, calm and a little mysterious."
}
```

描述是誰（年齡、男女、角色）、音色（低沉、明亮、沙啞）、速度和情緒。喜歡某個聲音後，記下它的 `seed`：換種子，聲音也可能
稍微改變。

### 音效

```json
{
    "graph": "sfx",
    "prompt": "a steel sword striking a metal shield, sharp metallic clang with a short ring, single hit, close",
    "seconds": 1.5,
    "avoid": "music, speech, background noise, low quality"
}
```

- `prompt`：寫出**聲源**、**動作**、**材質**和**錄音方式**：`"a big toad croaking three times, deep and wet, close,
  swamp"`。模型最能照著聲源和材質做。確切的次數（"three times"）不一定準。
- `seconds`：撞擊、吠叫、點擊用 1 到 3 秒；背景循環用 10 到 20 秒。最長 47 秒。
- `avoid`（可不填）：不想聽到的東西。不填時，預設會避開音樂、說話和雜音。
- 先做幾個版本（不填 `seed`），留下最好的。要重做它，從它的設定 `.json` 複製種子。

## 3. 給 AI 代理：無視窗模式

`ComfyLive.exe --make=<配方>` 最簡單：一個指令做一個檔案。要做很多檔案，或不寫檔就改數值，就先啟動一次伺服器：

```
ComfyLive.exe --headless --port=8766
```

然後用 TCP 連到 `127.0.0.1:8766`，每行送一個 JSON 指令，讀回事件（每行一個 JSON 物件），直到 `job_end` 事件：

```json
{"command": "make", "args": {"path": "D:/recipes/sfx_wolf_howl.json", "out": "D:/out/wolf.mp3"}}
{"command": "reset-graph", "args": {"graph": "speech"}}
{"command": "generate", "args": {"params": {"text": "Halt! Who goes there?", "speaker": "Aiden", "language": "English"}, "out": "D:/out/guard.mp3"}}
{"command": "comfyui-stop", "args": {"which": "all"}}
{"command": "shutdown"}
```

- `job_end` 有 `code`（0 成功、2 參數錯誤、4 沒有 ComfyUI、5 ComfyUI 拒絕流程、7 ComfyUI 失敗）和 `message`。
- `audio` 事件有 `path`（寫出的檔案）和 `settings`（它的 `.json`）。
- `get-params` 列出目前流程的每個數值，連同選項和範圍。
- 所有指令在 README 的「Headless mode」一節。

## 4. ComfyUI：只啟動一次，要你說才停

啟動 ComfyUI 要 15 秒到一分鐘，所以 **ComfyLive 從不自己停掉它**。它會在兩次執行之間持續執行，下一個聲音馬上開始。
共有兩個 ComfyUI：

| | 連接埠 | 執行 | Python |
|---|---|---|---|
| 主要 | 8188 | 圖片、音樂、音效 | `_comfyui/ComfyUI/.venv`（transformers 5.8） |
| 語音 | 8189 | 只有語音（Qwen3-TTS） | `_comfyui/ComfyUI/.venv-speech`（transformers 4.57.3） |

ComfyLive 會把每個流程送到對的那一個，需要時自動啟動。要停掉它們：

- 視窗裡的「停止 ComfyUI」按鈕（兩個都停）；
- `ComfyLive.exe --comfy-stop`（兩個）、`--comfy-stop=main` 或 `--comfy-stop=speech`；
- 無視窗指令 `comfyui-stop`。

視窗選項「關閉時停止我啟動的 ComfyUI」（預設關閉）會在關視窗時停掉它們。`--make` 和 `--headless` 加
`--stop-comfy-on-exit` 也一樣。

**GPU（12 GB）是共用的。** 每次執行前，ComfyLive 會請另一個 ComfyUI 卸載模型，語音模型也會在每次執行後離開 GPU，
所以兩個不會同時塞滿顯示卡。不過還是不要在同一時間做圖片和語音。

**為什麼要兩個。** 語音節點是為 transformers 4.57 做的。在主要 venv（5.8）裡它會亂講：2026-11-15 那句塔門的台詞，
Whisper 聽到的是「Thank you very much for watching」。把主要 venv 降版可能會弄壞其他節點，所以語音有自己的 venv。

## 5. 把聲音放進遊戲

遊戲只用 OGG Vorbis（[22_AUDIO_GENERATION.md](22_AUDIO_GENERATION.md) §3）。先轉檔，再用 key 加進
`assets/media/resources.json`（`sfx.*`）：

```
tools\ffmpeg\ffmpeg.exe -i D:\out\wolf.mp3 -c:a libvorbis -q:a 5 assets\media\sfx\wolf_howl.ogg
python tools/make_res_keys.py
```

太小聲的聲音（輕柔的背景聲常常如此）轉檔時順便把音量拉高：
`tools\ffmpeg\ffmpeg.exe -i in.mp3 -af loudnorm=I=-23 -c:a libvorbis -q:a 5 out.ogg`。

怪物發出的聲音要經過 `f_PlayFoeSound(foe, key)`，這樣怪物死時聲音會停止或淡出（專案規則「戰鬥效果屬於它的怪物」）。

## 6. 檢查做出來的東西

每個檔案放進遊戲前都要聽過。語音可以先快速檢查說的字對不對：用語音 venv 裡的 Whisper（語音轉文字）。它不判斷品質：
發音、重複和自然度要自己聽。2026-11-15 它聽到的英文和巫師台詞完全正確，中文故事只有幾個同音字不同。短的日文句子
「行くぞ！負けるもんか！」它聽成「行くぞ ハケルモンか」，可能是 Whisper 聽錯，不是語音的問題。這一句請自己聽。

## 7. 新電腦第一次設定

| 什麼 | 放哪裡 | 大小 |
|---|---|---|
| ComfyUI-Qwen-TTS 節點 | `_comfyui/ComfyUI/custom_nodes/ComfyUI-Qwen-TTS`（`git clone https://github.com/flybirdxx/ComfyUI-Qwen-TTS`） | 小 |
| Qwen3-TTS 模型 | `D:\Work\AI\models\qwen-tts\`（`Qwen3-TTS-Tokenizer-12Hz`、`Qwen3-TTS-12Hz-1.7B-CustomVoice`、`Qwen3-TTS-12Hz-1.7B-VoiceDesign`），由 `_comfyui/ComfyUI/models/qwen-tts` 連結過去 | 9.3 GB |
| 語音 venv | `_comfyui/ComfyUI/.venv-speech`：Python 3.11、torch 2.14.1 cu130、ComfyUI 的 requirements（含 `torchsde`）、節點的 requirements、`onnxruntime`、`transformers==4.57.3` | 約 6 GB |
| Stable Audio Open 1.0 | `D:\Work\AI\models\checkpoints\stable-audio-open-1.0.safetensors`：Hugging Face 上的 `model.safetensors`，**改名**後放這裡；需先同意授權（§7.1） | 約 5 GB |
| T5 編碼器 | `D:\Work\AI\models\text_encoders\t5-base.safetensors` | 850 MB |

確切的指令在 ComfyLive README 的「Speech」和「Sound effects」兩節。

### 7.1 取得 Stable Audio Open（帳號、授權、下載）

Stable Audio Open 是免費的，但 Stability AI 要求每個使用者用免費的 Hugging Face 帳號同意一次授權。沒有同意，下載會被拒絕
（HTTP 401），ComfyLive 的音效流程也會被拒絕，顯示
`Value not in list ckpt_name: 'stable-audio-open-1.0.safetensors'`。

1. **帳號。** 到 <https://huggingface.co/join> 用 email 註冊，再從收到的信確認。
2. **授權。** 登入後打開 <https://huggingface.co/stabilityai/stable-audio-open-1.0>，填上方的簡短表單，按
   **Agree and access repository**。之後「Files and versions」分頁會列出 `model.safetensors`。
3. **下載，兩種方法擇一：**
   - **用瀏覽器（最簡單）：** 在 <https://huggingface.co/stabilityai/stable-audio-open-1.0/tree/main>，按
     `model.safetensors` 那一列的下載箭頭（約 4.9 GB）。下載中瀏覽器會把它叫做 `Unconfirmed NNN.crdownload`
     （Chrome/Edge），下載完之前不要動它。
   - **用 token：** 在 <https://huggingface.co/settings/tokens> 按 **Create new token**，類型選 **Read**，然後複製
     （`hf_...`，不要給別人）。在終端機執行 `hf auth login`（擁有者電腦上的完整路徑是
     `C:\Users\leeyo\AppData\Local\Packages\PythonSoftwareFoundation.Python.3.11_qbz5n2kfra8p0\LocalCache\local-packages\Python311\Scripts\hf.exe`），
     貼上 token（貼上時不會顯示任何字；有些視窗要按右鍵貼上，Ctrl+V 沒用），「git credential」回答 **n**，等到出現
     **Login successful**。然後：
     `hf download stabilityai/stable-audio-open-1.0 model.safetensors --local-dir D:\Work\AI\models\checkpoints\sao`。
4. **改名和位置。** 它是一個**檔案**，不是資料夾。把 `model.safetensors` 改名為
   **`stable-audio-open-1.0.safetensors`**（最後有 **s**），直接放在 `D:\Work\AI\models\checkpoints\`。
   原因：ComfyUI 只用完全相同的檔名載入模型，而 ComfyLive 的音效流程、範例配方和每個存下來的設定 `.json` 都指定
   `stable-audio-open-1.0.safetensors`。另外，`model.safetensors` 是大多數模型都用的名字，再下載別的模型可能會蓋掉它。
   完整的檔案是 4,853,889,016 位元組。
5. **使用。** 在 ComfyLive 按「重新連線」（或重新啟動主要 ComfyUI），讓 ComfyUI 看到新檔案，再開啟
   `examples\sfx_dog_bark.json`，按「生成」。

**狀態（2026-11-15）：** 兩者都可以用。語音已照 §6 檢查過。14 個音效範例都做出來了，每個 2 到 5 秒，長度都符合要求。
森林和早晨鳥叫的背景聲很小聲（峰值約 −25 dB）：轉檔時把音量拉高（`-af loudnorm=I=-23`，§5）。

## 8. 授權

- **Qwen3-TTS：** 節點是 Apache-2.0，模型用 Qwen3-TTS 授權。在遊戲裡發布語音前請先讀過。
- **Stable Audio Open 1.0：** Stability AI Community License，年營收低於 100 萬美元免費。
- 不要叫任一個模型模仿真人的聲音。
