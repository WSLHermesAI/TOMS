# 17 — 粒子效果（`.particle`）

> 英文原文：[17_PARTICLES.md](../17_PARTICLES.md)

**現況：** 第 1 和第 2 階段已完成。
- **第 1 階段：** 格式、執行期 `toms::fx`、`fx` atlas、11 個範例效果、
  `toms_game --fx=`、測試。
- **第 2 階段：** 編輯器 `particle_editor`；第 5 節說明它。

接下來：第 2b 階段（渲染器中的混色模式）和第 3 階段（`.anim` 節點上的效果）。第 1
和第 2 節說明設計；第 3–5 節描述已經做好的東西。

TOMS 的新粒子系統：一種 JSON 檔案格式、一個 2D
執行期，和 `.anim`（[15](15_ANIMATION.md)）一樣從打包好的 sprite atlas（[14](14_ATLAS_TOOL.md)）繪製，
以及一個和動畫、atlas 編輯器共用程式碼的 Qt 編輯器。它
取代 FM79979 的粒子系統（`Core/GameplayUT/Render/Particle`、WinForms 的
`ParticleEditor`、`.prt` / `.prtg` XML）。

## 1. 舊系統做了什麼（以及要保留什麼）

**執行期**（`FM79979Engine/Core/GameplayUT/Render/Particle`，約 4k 行）

- `cPrtEmitter`：一張貼圖、一個固定大小的池（`MaxParticleCount`），以及發射設定：
  - **Gap**（每次發射間隔秒數）× **Amount**（每次發射的粒子數）× **EmitCount**
    （發射次數；0 = 無限）。
  - **速度**（`cPrtVelocityInitSetVelocity`）：一個方向向量加上隨機偏移。
  - **混色**：原始的 GL 混色列舉值。**圖元**：GL 四邊形或點。
- 行為來自**策略（policy）**，放在兩個有順序的清單中。**Init** 策略在粒子誕生時執行一次；
  **Act** 策略每個畫面執行：

  | 策略 | Init / Act | 做什麼 |
  |---|---|---|
  | `cPrtLifeInitrSetLife` | init | 壽命 = min + random(range) |
  | `cPrtLifeActDyingByGameTime` | act | 壽命 -= dt。**沒有它，粒子永遠不會死**，是個陷阱。 |
  | `cPrtColorInitrSetColor` / `...SetRandomColor` | init | 一個顏色，或在該顏色和白色之間隨機 |
  | `cPrtColorActBlending` | act | 顏色 ± rate·dt，限制在範圍內 |
  | `cPrtColorActBlendingByLife` | act | 在壽命期間從起始顏色內插到目標顏色 |
  | `cPrtColorActBlendingBy2Color` | act | 起始 → 顏色 1（壽命前半）→ 顏色 2（壽命後半） |
  | `cPrtSizeInitSetSize` | init | 大小，可選擇 × random(0..r) |
  | `cPrtSizeActBlending` | act | 大小 ± rate·dt |
  | `cPrtRotateInitRotate` / `cPrtRotateActRotate` | init / act | 一個角度，然後 角度 += 速度·dt（± 隨機） |
  | `cPrtStartPositionInitBySquareRange` | init | 方框中的隨機點 |
  | `cPrtStartPositionInitByFrame` | init | 附著在 3D 模型的 frame 上（只有 3D） |
  | `cPrtVelocityActAcceleration` | act | 沿起始方向加速 |
  | `cPrtVelocityActDircctionChange` | act | 每個軸：減速到停止時間，然後換新的速度 |
  | `cPrtVelocityActBySatelliteAction` | act | 類似軌道的偏移（有 bug：它把位置加到自己身上） |
  | `cPrtTextureActDynamicTexture` | act | 在粒子壽命期間播放 `.pi` 的翻頁影格 |

- `cParticleEmitterGroup` / `cParticleEmiterWithShowPosition`（`.prtg`）：一個群組放置一個或
  多個發射器。每個有位置、方向、開始和結束時間、循環，以及可選的曲線
  **路徑**（`.path` 檔）可沿著移動。

**檔案。** 每個發射器的設定是以逗號分隔的字串，塞在 XML 屬性中，所以
意義取決於數字的順序：

```xml
<Particle TPVersion="1.0">
  <TextureList Name0="Default.png" />
  <Emiter Name="Fire" Data="0.032,0,1,10000,772,772,7,0.00,-17.00,0.00,1,20.00" Texture="Default">
    <InitPolicy Type="cPrtLifeInitrSetLife" Data="0.50,1.00,1" />
    <InitPolicy Type="cPrtSizeInitSetSize" Data="70.00,100.00" />
    <ActPolicy Type="cPrtLifeActDyingByGameTime" />
    <ActPolicy Type="cPrtColorActBlendingBy2Color" Data="1,1,1,1,0,0,0,0" />
  </Emiter>
</Particle>
```

**編輯器**（WinForms）：一張很長的表單，有策略清單、每種策略類型一個子表單、原始的
GL 混色下拉選單，以及 3D 鏡頭選項（透視、X 旋轉）。群組編輯器是
另一個視窗。

**要修正的問題**

1. **資料無法閱讀：** 依位置排列的逗號字串，以及 GL 列舉數字（`772`、`7`）。
2. **行為取決於清單順序，也取決於記不記得加策略：** 少了 `DyingByGameTime`
   就是不死的粒子，兩個顏色策略會互相打架。
3. **大部分「act」策略是速率**（每秒 ±），不是目標值。「在壽命結束前淡到 0」
   無法直接表達，而且結果會隨壽命長度改變。
4. **每個發射器一張散落的貼圖，不在任何 atlas 中：** 每個發射器都是自己的 draw call 和
   自己的貼圖。
5. **只有 3D 才用的部分**（模型 frame、透視鏡頭、compute shader、點圖元），
   TOMS 都用不到。
6. **路徑放在另一個 `.path` 檔，** 而 TOMS 已經有更好的動作工具
   （`.anim`）。

**保留：** 粒子池；依速率加上發射次數的發射方式；壽命 / 顏色 / 大小 / 旋轉 /
速度行為；翻頁影格；帶偏移和開始/結束時間的發射器群組；
一次性和循環效果。

## 2. 新設計摘要

- **固定的模組組合，取代策略清單。** 發射器永遠有同樣的區段：
  發射、形狀、起始值、壽命曲線、力、翻頁動畫和渲染。區段
  出現在檔案中就會使用，在編輯器中勾選它的核取方塊就會開啟。沒有任何東西
  取決於順序，也不需要任何東西只為了讓粒子死亡。
- **用範圍取代「隨機」旗標。** 任何起始值都是一個數字或 `[min, max]`。
- **用正規化壽命（0..1）上的曲線取代速率。** 它們使用和 `.anim` 相同的 key 和緩動名稱
  （Tweeny 緩動和貝茲曲線），所以「在最後 30% 淡出」是一條曲線，
  適用於任何壽命長度。
- **Sprite 來自 atlas，** 和 `.anim` 完全一樣以 `"id:name"` 引用，使用同一個
  `AtlasSet`。效果會和同一頁面上繪製的其他東西一起批次處理。
- **只有 2D：** 像素、y 往下、角度、順時針旋轉，和 `.anim` 相同的慣例。
- **混色可用預設名稱或任何 src/dest 組合**（第 3a 節）。bgfx 接受任何混色係數；
  渲染器的 `Quad::additive` 變成一個小的混色 id，批次依它切分。
- **結果確定：** 每個效果實例都有一個種子，所以編輯器預覽、`toms_game` 和
  截圖測試永遠產生相同的粒子。
- **效果和動畫一起播放：** `.anim` 節點可以帶一個跟著節點的效果
  （取代 `.prtg` 路徑），動畫事件可以觸發一次性的爆發。

## 3. 檔案格式（`.particle`，JSON）

一個檔案容納多個**效果**。效果是一組有名稱的**發射器**（也就是以前的 `.prtg`）。
副檔名是 `.particle`；不用 `.fx`，因為 Visual Studio 會把它當成 shader。

```jsonc
{
  "version": 1,
  "atlases": ["../atlas/game.atlas", {"id": "fx", "path": "../atlas/fx.atlas"}],   // as .anim
  "effects": [
    {
      "name": "torch_fire",
      "duration": 0,              // seconds; 0 = endless (until stopped)
      "loop": false,              // with a duration: start over at the end (bursts fire again)
      "prewarm": 1.0,             // seconds simulated before the first frame (an already-burning fire)
      "seed": 0,                  // 0 = a new seed each play; otherwise fixed
      "emitters": [
        {
          "name": "flames",
          "offset": [0, 0],         // from the effect's origin, px
          "start": 0, "stop": 0,    // active window in effect time (stop 0 = until the effect ends)
          "space": "world",         // world: particles stay where they were born; local: they move with the effect
          "maxParticles": 120,

          "emission": {
            "rate": 40,                                   // per second while active
            "bursts": [ { "t": 0, "count": [8, 12], "repeat": 0, "interval": 0.5 } ]
          },
          "shape": { "type": "circle", "radius": 6, "arc": [0, 360], "edge": false, "outward": false },
          //  point | line {length, angle} | box {size: [w, h]} | circle {radius, arc} | ring {radius, thickness, arc}
          //  edge: only on the outline (box, circle); outward: fly away from the centre instead of
          //  "direction" (a point: every direction; spread still applies)

          "life":      [0.5, 0.9],    // seconds
          "direction": -90,           // degrees; -90 = up (y down)
          "spread":    25,            // ± degrees around direction
          "speed":     [40, 70],      // px / s
          "size":      [20, 28],      // px (width; height follows the sprite's aspect)
          "rotation":  [0, 360],      // start angle, degrees
          "spin":      [-90, 90],     // degrees / s
          "color":     [[1, 1, 1, 1], [1, 0.85, 0.7, 1]],   // random between two colours (or one)
          "sprite":    "fx:flame",

          "overLife": {               // t = normalised life 0..1; keys and eases as in .anim
            "color": [ {"t": 0,   "v": [1, 0.9, 0.5, 0]},
                       {"t": 0.15,"v": [1, 0.7, 0.2, 1]},
                       {"t": 1,   "v": [0.5, 0.1, 0, 0], "ease": "quadraticIn"} ],   // multiplies "color"
            "size":  [ {"t": 0, "v": 0.6, "ease": "quadraticOut"}, {"t": 1, "v": 1.4} ],   // × start size
            "speed": [ {"t": 0, "v": 1}, {"t": 1, "v": 0.3} ],                            // × start speed
            "spin":  [ {"t": 0, "v": 1}, {"t": 1, "v": 0} ]                               // × start spin
          },
          "forces": {
            "gravity":    [0, -60],   // px / s² (negative y = rises)
            "drag":       0.8,        // 1/s, velocity *= e^(-drag·dt)
            "radial":     [0, 0],     // px / s² away from the emitter (min..max)
            "tangential": [0, 0]      // px / s² around the emitter, clockwise (orbits; replaces "satellite")
          },
          "flipbook": {               // replaces cPrtTextureActDynamicTexture
            "frames": ["fx:flame_0", "fx:flame_1", "fx:flame_2"],
            "mode": "overLife",       // overLife: all frames across the life; fps: loop at "fps"
            "fps": 12, "randomStart": true
          },
          "render": {
            "blend": "add",           // normal | add | multiply | screen | {"src": "srcAlpha", "dst": "one"}
            "order": 0,               // between emitters of this effect (higher = in front)
            "alignToVelocity": false, // rotate each particle to face its motion (sparks, streaks)
            "oldestOnTop": false      // default: newer particles draw over older ones
          }
        }
      ]
    }
  ]
}
```

（註解要點：`duration` 0 = 無限；`loop` 在有持續時間時結束後重來；`prewarm` 在第一個畫面前先模擬幾秒（已經在燒的火）；
`seed` 0 = 每次播放用新種子；`space` world = 粒子留在誕生處，local = 跟著效果移動；`shape` 的種類有
point / line / box / circle / ring，`edge` 只在外框，`outward` 從中心往外飛；`direction` -90 = 向上；`spread` 為方向兩側 ± 角度；
`size` 是寬度，高度依 sprite 比例；`color` 在兩個顏色之間隨機；`overLife` 的 t 是正規化壽命，值乘以起始值；
`gravity` 的 y 為負 = 上升；`drag` 為 1/秒；`tangential` 繞發射器順時針（軌道）；`flipbook` 的 `overLife` 模式在整個壽命播完所有影格，`fps` 模式依 fps 循環；
`render.order` 是本效果發射器之間的順序；`alignToVelocity` 讓粒子朝向運動方向；`oldestOnTop` 預設新粒子畫在舊粒子上面。）

**規則**

- 任何欄位都可以省略；預設值是白色、大小 16、壽命 1 秒、每秒 10 個、速度 50、向上、
  沒有力、`normal` 混色、最多 100 個粒子。
- **未知的欄位是錯誤**（`"colour"` 會連同位置一起被回報），所以打錯字永遠不會默默
  沒有作用。
- 起始值是一個數字或 `[min, max]`。`color` 則是一個顏色，或兩個可供挑選的顏色。
- 曲線 key 是 `{t, v, ease?}`，`t` 從 0 到 1，和 `.anim` 一樣。第一個 key 之前，
  值是第一個 key 的值；最後一個 key 之後是最後一個 key 的值。壽命曲線的
  值乘以起始值（顏色逐通道、大小、速度、自轉）。
- Sprite 是 `"atlasId:name"` 或不帶前綴的 `"name"`，透過和 `.anim` 相同的 `AtlasSet` 查找。
- `checkParticles`（由 `--fx` 顯示在 log 中，之後也在編輯器的問題面板中）：
  - **錯誤：** 未知的 sprite、曲線 `t` 超出 0..1 或重複、`stop` 早於
    `start`、`min` 大於 `max`、沒有 sprite、重複的爆發沒有間隔、重複的
    效果名稱。
  - **警告：** 速率 × 壽命超過 `maxParticles`（有些粒子無法誕生）、爆發比
    池還大、`dstAlpha` 係數、有 `loop` 但沒有持續時間。
- `multiply`、`screen` 和自訂組合現在會儲存並檢查，但在第 2b 階段之前都以 `normal` 繪製
  （目前只有 `add` 有自己的混色狀態）。

**效果的時間。** 有 `duration` 的效果在結束時停止產生粒子，並在最後一個粒子死亡時
`finished()`（有 `loop` 的話則重新開始）。爆發在發射器時間 `t` 觸發，然後每隔
`interval` 再觸發 `repeat` 次（`-1` = 只要發射器還在作用就一直觸發）。

**範例：** [examples/fx_recipes.particle](../examples/fx_recipes.particle) 有 11 個效果：
- `torch_fire`（翻頁火焰 + 餘燼 + 煙）、`hit_sparks`、`slash_ring`、`heal`
  （local 空間）
- `magic_circle`（軌道）、`smoke_puff`、`dust_landing`、`coin_burst`（遊戲 sprite）、
  `level_up`、`snow`、`dark_aura`（multiply）

它們使用 **fx atlas** `assets/media/atlas/fx.atlasproj`：白色、可上色的 `dot`、`glow`、
`spark`、`star`、`ring`、`smoke`、`flame_0..3` 和 `square`，由
`tools/atlas/make_fx_sprites.py` 產生，再用 `atlaspack` 打包。

### 3a. 混色

混色決定粒子的顏色（**src**）如何和畫面上已有的東西（**dst**）結合：
`result = src × srcFactor + dst × dstFactor`。

| 預設 | src、dst 係數 | 看起來像 | 用於 |
|---|---|---|---|
| `normal` | srcAlpha、1 − srcAlpha | 塗在上面，蓋住後面的東西 | 煙、塵、碎片、葉子 |
| `add` | srcAlpha、one | 加上光，只會變亮，重疊處會發光 | 火、火花、魔法、光暈 |
| `multiply` | dstColor、0（alpha → 白） | 像有色玻璃一樣變暗，白色 = 不變 | 陰影、焦痕、黑煙 |
| `screen` | one、1 − srcColor | 柔和地變亮，永遠不會爆成純白 | 柔光、薄霧、治療光暈 |

其他任何組合是 `{"src": f, "dst": f}`，係數有 `zero`、`one`、`srcColor`、
`invSrcColor`、`srcAlpha`、`invSrcAlpha`、`dstColor`、`invDstColor`、`dstAlpha`、`invDstAlpha`。
這也涵蓋了所有舊的 GL 組合。編輯器在下拉選單中顯示預設，「Custom…」
會打開兩個係數下拉選單，並在亮和暗的背景上即時預覽。

備註：
- 貼圖是 straight alpha（`fs_sprite.sc`），所以 `multiply` / `screen` 隨 alpha 淡化的做法，是
  shader 先把顏色往白 / 黑混合。那只是一個小的 shader uniform，不是新的
  管線。
- `.anim` 節點也會有同樣的選擇。目前它們有 `"blend": "add"`；預設和自訂
  組合會以同樣方式加入，舊的值仍然有效。
- `dstAlpha` 係數取決於渲染目標有沒有 alpha。螢幕沒有，所以它們
  的行為像 `one` / `zero`，編輯器會發出警告。

**`.anim` 如何使用效果**（第 3 階段）：

```jsonc
// on a node: an effect that follows the node (position, rotation, colour, visibility)
{ "name": "torch", "sprite": "torch", "effect": "fx/fire.particle#torch_fire" }
// as an event: a one-shot burst at the node's position when the time passes the key
"tracks": { "event": [ { "t": 0.28, "v": "fx:fx/hit.particle#spark_burst" } ] }
```

（在節點上：跟著節點（位置、旋轉、顏色、可見性）的效果；作為事件：時間經過 key 時，在節點位置觸發一次性爆發。）

這取代 `.prtg` 路徑：要讓發射器沿著曲線移動，就讓節點動起來，並把
效果附在它上面。

## 4. 執行期（`src/core/engine/particle_fx.h/.cpp`，`toms::fx`）

- 帶有 `parseParticles` / `particlesToJson` 的 `ParticleFile`，和 `anim_clip` 對應。
```cpp
toms::fx::ParticleFile file;   toms::fx::parseParticles(text, file, &err);
toms::fx::EffectInstance fx;
fx.setTransform(toms::anim::placement(x, y));   // first: prewarm makes particles where it is
fx.play(file.find("torch_fire"), seed);           // seed 0 = the effect's, else a new one
// every frame:
fx.setTransform(toms::anim::placement(x, y));    // when it moves
fx.update(dtSeconds);
fx.appendQuads(atlases, camera, nullptr, quads);  // then ren->drawSprite(q) for each
if (fx.finished()) ...                            // a one-shot is done
```

（先 `setTransform`：prewarm 會在那個位置產生粒子；每個畫面：移動時 `setTransform`、`update`、`appendQuads` 後逐一 `drawSprite`；`finished()` 表示一次性效果結束。）

- 帶有 `parseParticles` / `particlesToJson` / `checkParticles` 的 `ParticleFile`，和
  `anim_clip` 對應。Atlas 和 `"id:name"` sprite 走和 `.anim` 相同的 `AtlasSet`。
- `EffectInstance`：
  - **API：** `play`、`stop(clear)`、`update`、`seek(t)`（用相同種子重新播放到時間 t，
    給編輯器拖曳用）、`emitting`、`finished`、`liveCount`。
  - **儲存：** 每個發射器一個池，structure-of-arrays，大小為 `maxParticles`，`play` 之後
    不再配置記憶體。死掉的粒子會被壓縮掉，所以池保持誕生順序
    （繪製順序）。
- `appendQuads`：四個角、UV、色調、加法和貼圖，和 `.anim`
  產生的 `Quad` 相同。發射器依 `order` 繪製。粒子的大小是寬度；高度跟著
  sprite 的比例。
- **固定 1/60 秒的步長**（剩下的時間帶到下一次更新；每次更新最多 8 步），
  所有隨機性都來自實例的種子。相同的種子在任何畫面更新率下都產生相同的
  粒子：60 × 1/60 秒和 40 × 0.025 秒結果相同。
- **成本：** 2,000 個存活粒子每個畫面花 0.075 ms（更新 + 四邊形，原生 release 版；
  由 `particle_fx_test` 印出）。
- **硬體路徑（渲染器，不是粒子）：** 繪製粒子、`.anim` 節點和 sprite 的 sprite 批次，
  在 GPU 上建立它的頂點。
  - **怎麼做：** instancing，`vs_sprite_inst.sc`。每個四邊形一個 64 位元組的 instance（它的四個角、uv
    矩形、色調），而不是 CPU 寫入四個頂點和六個索引。
  - **在哪裡：** 只要後端有 instanced 程式就會使用，目前 TOMS 執行的每個後端都有
    （D3D11/12、Vulkan、OpenGL、GLES3 / WebGL2）。這個版本的 bgfx 已經沒有
    instancing 能力旗標：它支援的每個後端都能 instancing。
  - **後備：** 沒有它時（或用 `toms_game --no-instancing`）走 CPU 路徑。
  - **相同的像素：** 兩條路徑在四個 Windows 後端上都逐像素相同；`smoke.fx_cpu`
    確保如此。
  - **這取代了 FM79979 的 `cParticleBatchRender`。** 那個用 compute shader 做同樣的
    頂點展開，然後把頂點讀回 CPU；instancing 不需要 compute（WebGL2 沒有）
    也不需要讀回。
  - **三條路徑**（`--sprite-path=`，全部逐像素相同，各由一個截圖測試確保）：
    - **instancing**（預設）
    - **compute：** `cs_sprite.sc` 把四邊形展開到 GPU 頂點緩衝區，由繪製讀取；這就是
      沒有讀回的 FM79979 `cParticleBatchRender`。
    - **cpu**
  - **實測**（D3D11，F3 列，每個粒子都存活並被繪製）：

    | 存活粒子 | CPU 頂點 | GPU instancing | GPU compute |
    |---|---|---|---|
    | 30,000 | 1.70 ms（588 FPS） | 1.37 ms（730 FPS） | 1.76 ms（569 FPS） |
    | 300,000 | 27.1–27.8 ms（36–37 FPS） | 19.3–21.8 ms（46–52 FPS） | 19.0–20.2 ms（50–53 FPS） |

    「auto」選 instancing：它和 compute 一樣快，而且到處都能用。
  - **所有路徑都使用持久的 GPU 緩衝區**，需要時會成長。以前 CPU 和 instancing
    路徑用 bgfx 每個畫面的暫時緩衝區（幾 MB）：在 300,000 個四邊形時，每個畫面會
    丟掉大約 256,000 個四邊形，所以只畫了大約 44,000 個。F3 列會顯示
    `drawn/asked quads` 和效果的存活粒子，所以這種丟失不會再被隱藏。
  - **CPU 時間花在哪裡**（`particle_fx_test`，30,000 個粒子）：大約 0.23 ms 在
    運動（更新），大約 1.2 ms 在建立四邊形（顏色 / 大小曲線、旋轉、角）。GPU
    模擬（下方）把兩者都去掉了。

**GPU 模擬**（大型發射器；`cs_fx_spawn.sc` + `cs_fx_update.sc`）

- **移到 GPU 的部分：** 整個粒子更新。也就是位置、速度、重力、
  阻力、徑向 / 切向力、壽命期間的顏色 / 大小 / 速度 / 自轉（曲線烘焙成
  64 項的表）、翻頁影格和旋轉，再加上四邊形。頂點直接寫入
  繪製讀取的緩衝區，不會讀回任何東西。
- **留在 CPU 的部分：** 產生粒子（什麼時候、在哪裡、有種子的隨機值、一個空位），所以
  GPU 發射器產生的粒子和 CPU 會產生的相同。CPU 知道每個空位何時釋放，
  因為粒子的壽命在誕生時就固定了，所以 `liveCount()`、`finished()` 和池的上限
  和以前一樣運作（`particle_fx_test`：每個畫面的存活數都相等）。
- **哪些發射器：** `EffectInstance::setGpuSimulation(renderer, threshold)` 設定它，
  每個發射器的 `"simulation"` 決定：

  | `"simulation"`（在粒子編輯器的 Render > Simulation 設定） | 模擬於 |
  |---|---|
  | `"auto"`（預設） | `maxParticles` 超過遊戲門檻時用 GPU，否則用 CPU |
  | `"gpu"` | 裝置有 compute shader 時一律用 GPU，否則用 CPU |
  | `"cpu"` | 一律用 CPU |

- **門檻** 是遊戲設定 `particleGpuThreshold`（`save/settings.json`；0 = 永不）。
  預設**桌機 5000、手機 3000**。`toms_game --fx-gpu-threshold=N`
  會在一次執行中覆寫它。
- **在哪裡執行：** D3D11/12、Vulkan、OpenGL 4.3。瀏覽器（WebGL2）和 GLES3 手機沒有
  compute shader，所以那裡的每個發射器都在 CPU 上模擬。粒子編輯器的預覽
  永遠在 CPU 上模擬。
- **和 CPU 一致：** 火、魔法陣和雪在 GPU 上模擬時，在 D3D11/12、Vulkan 和 OpenGL 上
  和 CPU 最多只差幾個顏色階（曲線表、浮點捨入）。
  `smoke.fx_gpu` 用和 `smoke.fx` 相同的參考圖檢查它。
  - **繪製順序不同：** 同一個 GPU 發射器的粒子依空位順序繪製，不是誕生順序。
    用 `add` 混色時看不出來，但 `normal` 混色的粒子重疊處可能會有些微差異。
- **實測**（300,000 個存活粒子，D3D11）：GPU 模擬 78–85 FPS（11.7–12.8 ms），CPU
  模擬 33–35 FPS（28.7–30.4 ms），大約快 2.4 倍。
- **F3 列：** 顯示 `GPU n/m emitters (>threshold)`。
- **預覽：** `toms_game --fx=<file>#<effect>` 在畫面上的任何東西上方置中繪製效果，
  種子 1，一次性效果結束 0.5 秒後重播。問題和缺少的 sprite 會寫到
  log（`[fx] ...`）。
- **測試：**
  - `unit.particle_fx_test`：JSON、發射、運動、四邊形、種子、檢查、用真正的 atlas 跑範例，
    以及效能測試。
  - `smoke.fx`：地圖上的 `torch_fire`，和 `tests/golden/fx.png` 比對。
  - `atlas.fx_up_to_date`：fx atlas 和它的專案一致。

## 5. 編輯器介面（`particle_editor`，Qt）

```
particle_editor [file.particle]                                       the editor
particle_editor --headless check file.particle                        parse + check; exit 0 ok, 2 errors, 3 usage
particle_editor --headless render file.particle#effect out.png [frames] [every]
                                                                      a contact sheet (8 moments, 0.15 s apart)
particle_editor --selftest file.particle outdir                       automated check (-platform offscreen; QPainter preview)
particle_editor --selftest-gpu file.particle outdir                   the same for the game-renderer preview (on screen)
```

（`--headless check`：解析並檢查，結束碼 0 正常、2 錯誤、3 用法錯誤；`--headless render`：一張縮圖表（8 個時刻，相隔 0.15 秒）；
`--selftest`：自動檢查（離螢幕、QPainter 預覽）；`--selftest-gpu`：遊戲渲染器預覽的同樣檢查（在螢幕上）。）

**復原、歷程和備份** 和 atlas、動畫編輯器一樣運作（[15](15_ANIMATION.md)）：Ctrl+Z / Ctrl+Y、歷程面板，以及自動備份（檔案 > 備份）。

**預覽使用遊戲的渲染器。** Viewport 是一個 `GameCanvasView`（和 atlas
及動畫編輯器共用，`tools/studio_common`）：一個由 toms_game 的 `BgfxRenderer` 繪製的
原生視窗（`ParticleViewportGpu.cpp`），gizmo 和狀態列則以 QPainter 覆蓋層畫在上面：
- **和遊戲相同：** 相同的 sprite 批次、貼圖（點取樣 sRGB）、混色和 GPU
  粒子模擬。設為 `"gpu"` 的發射器，或超過工具列 **GPU sim
  above** 值（5000，和桌機遊戲設定相同；0 = 永不）的 `"auto"` 發射器，會在 GPU 上模擬，
  就和遊戲一樣。
- **也透過那個渲染器繪製的：** 背景、格線、原點和 gizmo（以純色四邊形）。
  狀態列是 bgfx 除錯文字：後端、FPS、存活粒子、GPU 模擬的發射器和
  draw call。
- **何時退回 QPainter（CPU 模擬）：** 檢視 > *用遊戲渲染器預覽*
  （關閉；重新啟動後生效）、離螢幕的 selftest、bgfx 啟動失敗，以及
  `--headless render`。
- **由 `--selftest-gpu` 檢查：** Direct3D 11 上 60 FPS（vsync）；gizmo 在原生
  視窗中能用。編輯器中 GPU 和 CPU 模擬的比較：
  - 魔法陣和打擊火花在幾個顏色階之內相同；
  - 火把有 0.18% 的像素不同：翻頁影格可能因為極小的年齡差異而切換，
    而 normal 混色的煙依空位順序繪製；
  - 雪有 0.47% 不同：隨機旋轉 300 步之後，雪花大約漂移一個像素。

Visual Studio：**「particle_editor (recipes)」** 目標。`--headless render` 是不開編輯器、
快速檢視某人（或 AI）寫的效果的方法。

**已經做好的東西**（tools/particle/qt；下方草圖是當初的計畫，編輯器照著它做）：

- **Effects 面板：** 效果和它們的發射器。
  - 核取方塊只在預覽中隱藏發射器；雙擊 / F2 重新命名。
  - 工具列可新增效果和發射器、複製、刪除，以及上下移動發射器。
- **Viewport：** 遊戲自己的模擬（`EffectInstance`），以遊戲的方式繪製。
  - 選取發射器的 **gizmo**：拖曳中央方塊移動它，拖曳箭頭改變它的
    方向（Shift：15° 一格），拖曳弧的兩端改變擴散角，拖曳圓形控制點改變形狀的
    大小。
  - **移動效果：** 點一下就在那裡觸發效果；Ctrl+拖曳在播放時移動它
    （world 空間的粒子會留在後面）；雙擊放回 0,0。
  - **視圖：** 右鍵拖曳 / 中鍵 / Space+拖曳 平移，滾輪縮放。
  - **把 sprite 拖到** viewport 中，會在那裡建立新的發射器（多個：翻頁動畫）。
  - **狀態列：** 時間、存活粒子、四邊形、批次、步長時間、種子。
- **播放工具列：**
  - 播放 / 暫停（Space）、重新開始（R）、單步（.）。
  - 重播一次性效果，速度 0.1×–2×。
  - 預覽種子和 New Seed；背景（棋盤格、暗、亮、一張圖片）；格線。
  - 每次編輯後，預覽會用相同的種子重播到同一時刻，所以變更
    立即顯示，不必從頭開始。
- **屬性面板：** 每個模組一個可收合的區段。
  - 範圍是 min–max 加上一個連結（連結 = 一個值）。
  - **壽命期間的顏色** 是漸層條：拖曳色標，雙擊色標設定它的顏色，
    雙擊漸層條新增色標，右鍵刪除。
  - **壽命期間的大小 / 速度 / 自轉** 是曲線圖：拖曳 key，雙擊新增，
    右鍵刪除，選取 key 的緩動在下方設定。
  - **爆發** 是一個表格。**翻頁影格** 接受拖放的 sprite，並可拖曳
    重新排序。
  - **混色：** 一個預設，或附 src / dst 係數的「custom…」。
  - **形狀：** 只顯示它的類型用到的欄位。
- **Sprites 面板：** 每個 atlas 一個分頁，附縮圖。
  - 雙擊：選取的發射器改畫那個 sprite。
  - 把 sprite 拖到 viewport 建立新的發射器，或拖到翻頁影格上。
  - + / − 新增或移除 atlas（和 `.anim` 一樣，以相對於 `.particle` 的路徑存放）。
- **時間軸：** 一把可以拖曳的尺（暫停時；用種子重播，所以是精確的），每個
  發射器一條道，上面有它的開始..停止條和爆發刻度。拖曳條的兩端或中間。
- **Problems / Log：** 即時的 `checkParticles`（雙擊會選取發射器），加上開啟 /
  儲存的檔案。
- **Effect > Add Preset：** 11 個食譜效果中的任何一個（內建在編輯器中），連同
  它們需要的 atlas。
- 每次編輯都可以**復原 / 重做**；按住數值框箭頭或一次拖曳算一步。

**尚未完成：** 動畫編輯器不會顯示效果（第 3 階段），multiply / screen / 自訂
混色都以 normal 繪製（第 2b 階段），編輯器和遊戲中都一樣。

計畫的配置草圖：

```
┌ File  Edit  Effect  View  Help ───────────────────────────────────────────────────────────────┐
│ [New ▾ preset] [Open] [Save] │ ▶ Play  ⏸  ⟲ Restart  ⏭ Step │ Speed 1× │ Loop ☑ │ Seed 1234 🎲 │
├──────────────────┬─────────────────────────────────────────────┬──────────────────────────────┤
│ Effects          │                                             │ Inspector: flames            │
│ ▾ torch_fire     │                                             │ ▾ ☑ Emission                  │
│    👁 flames      │            (viewport: CanvasView)           │    Rate   [ 40 ]/s            │
│    👁 smoke       │                                             │    Bursts  t  count  repeat    │
│    👁 embers      │          ·  ·  ✦ ·                          │            0  8–12   0   [+]  │
│ ▸ hit_spark      │        ·  ✦✦✦  ·      ← particles           │ ▾ ☑ Shape  [circle ▾]         │
│ ▸ heal           │          ╲ │ ╱        ← spread arc           │    Radius [6]  Arc [0]–[360]  │
│                  │           ⊕──→      ← emitter + direction    │ ▾ Start values                │
│ [+ Emitter]      │          ( ◯ )      ← shape handle           │    Life  [0.5]–[0.9] s  🔗     │
│ [Duplicate] [🗑]  │                                             │    Speed [40]–[70]  Dir [-90°]│
│                  │  BG: ▣ checker ▾  Grid ☑  Map shot ☐         │    Spread [25°] ◔             │
│                  │  live 87/120 · quads 87 · batches 2 · 0.2ms │    Size [20]–[28]  Rot  Spin  │
│                  │                                             │    Color [■]–[■]  Sprite [🔥] │
├──────────────────┴─────────────────────────────────────────────┤ ▾ ☑ Over life                 │
│ Sprites: [game] [fx] ── (thumbnails, drag onto Sprite / Flip-  │    Color ▕██████▓▓▒░▏ gradient│
│          book fields or into the viewport for a new emitter)   │    Size  ╭──╮ curve           │
│ Timeline: flames ████████████████  smoke   ░░████████  (start/ │    Speed ╲___ curve           │
│           embers ▮ ▮ ▮ bursts      stop bars; scrub)           │ ▸ ☐ Forces  ▸ ☐ Flipbook      │
│ Problems / Log                                                 │ ▾ Render  Blend [add ▾] ...   │
└────────────────────────────────────────────────────────────────┴──────────────────────────────┘
```

- **Effects 樹：** 效果和它們的發射器，附眼睛（隱藏）和 solo 開關、重新命名、
  複製，以及拖曳重新排序（繪製順序）。有一鍵從預設新增：
  - 火、煙、火花、打擊爆發、治療閃光、魔法陣、雪、雨、塵埃、金幣
    爆發、升級光柱。
- **Viewport：**
  - 發射器顯示為 gizmo：拖曳 ⊕ 移動它，拖曳箭頭設定方向和速度，
    拖曳弧的兩端設定擴散角，拖曳形狀控制點調整大小。
  - 點任何地方就在那裡觸發效果（給爆發用）。Ctrl+拖曳移動效果的原點，
    觀察 `world` 和 `local` 空間的軌跡差異。
  - 背景：棋盤格、純色，或遊戲截圖，用來判斷加法效果在
    真正美術上的樣子。
  - 統計列：存活粒子、四邊形、批次（貼圖或混色改變）、更新時間。
- **屬性面板：**
  - 每個模組一個可收合的區段，各有一個啟用核取方塊（Unity 風格）。
  - **範圍 widget：** min–max 加上連結開關，所以一個值可以代表「不隨機」。
  - **漸層編輯器** 用於壽命期間的顏色和 alpha：新增、移動或刪除色標；顏色選擇器；
    每個色標的緩動。
  - **曲線編輯器** 用於壽命期間的大小、速度和自轉，重用 `.anim` 的貝茲/緩動對話框。
  - Sprite 欄位接受從 Sprites 面板拖放。翻頁動畫可以接受多個選取的 sprite，
    和動畫編輯器的 sprite 序列一樣。
- **時間軸：** 在效果的持續時間上，每個發射器一條（開始/停止區間、爆發刻度）。
  拖曳條和刻度，並拖曳以預覽任何時刻（從 0 用種子模擬，
  所以拖曳是精確的）。
- **問題：** 和 `--headless check` 相同的檢查。
- **無介面：**
  - `particle_editor --headless check x.particle`
  - `--render x.particle#effect --frames N out.png`：給審查和 AI 製作的效果用的
    縮圖表。
- **另外：** 每次編輯都可以復原/重做、未儲存工作的自動儲存，以及最近開啟的檔案。

## 6. 舊格式 → 新格式對照（如果之後要做轉換器）

| 舊的 | 新的 |
|---|---|
| 發射器 `Data`：gap、emitCount、amount、max | `emission.rate` = amount / gap（emitCount 0）；有限的 emitCount 變成 `bursts`（count = amount，repeat = emitCount − 1，interval = gap） |
| `Data` 速度 x、y、z + 隨機偏移 | `direction` = atan2(y, x)，`speed` = 長度，`spread` ≈ atan(偏移 / 長度) |
| src/dest 混色 `770,1` / `770,771` / `772,772` | `add` / `normal` / `normal`（+ 警告） |
| `LifeInitrSetLife min, range, random` | `life: [min, min + range]`（或 `min`） |
| `LifeActDyingByGameTime` | （永遠開啟） |
| `ColorInitrSetColor` / `SetRandomColor c` | `color: c` / `color: [c, [1, 1, 1, 1]]` |
| `ColorActBlendingByLife c` | `overLife.color` 起始 → c（以相對於起始顏色的乘數表示） |
| `ColorActBlendingBy2Color c1, c2` | `overLife.color` 的 key 在 0、0.5 = c1、1 = c2 |
| `ColorActBlending rate` | `overLife.color` 在 1 的 key = 起始 ± rate·壽命（近似，會警告） |
| `SizeInitSetSize x, y, random` | `size: [x·(1 − r), x]`（比例取自 y/x） |
| `SizeActBlending rate, add` | `overLife.size` 在 1 的 key = 1 ± rate·壽命 / 大小（近似） |
| `RotateInitRotate z` / `RotateActRotate z, random` | `rotation: z` / `spin: z`（± 隨機） |
| `StartPositionInitBySquareRange w, h` | `shape: box [2w, 2h]` |
| `VelocityActAcceleration a` | `overLife.speed` 斜坡，或 `forces.radial` |
| `VelocityActDircctionChange` | `overLife.speed` 在停止時間降到 0，然後 `forces.gravity` = 新的速度 |
| `VelocityActBySatelliteAction` | `forces.tangential`（舊的有 bug；會警告） |
| `TextureActDynamicTexture`（`.pi` 影格） | `flipbook.frames`（用 `atlaspack import` 把 `.pi` 圖片匯入 atlas） |
| `.prtg` 群組：發射器、位置、方向、開始/結束、循環 | 一個效果，發射器設 `offset`、`direction`、`start`/`stop` |
| `.prtg` 路徑（`.path` 曲線） | 一個有 `pos` key 的 `.anim` 節點，並附上效果 |
| 模型 frame、透視、compute shader、點、KillOutRange | 捨棄（只有 3D 用） |

舊檔案有 7 個（`MagicTower/.../Media/ParticleData`：Fire、Glyph、Smoke、Snow、
Stage1 `.prt`，以及 Snow 和 Stage1 `.prtg`）。轉換器（`atlaspack` 風格的 CLI，
`particle_editor --import x.prt`）很小。和 `.pi`/`.mpdi` 一樣，列為之後再做。

## 7. 階段

| 階段 | 內容 | 完成條件 |
|---|---|---|
| **1. 執行期 + 格式** ✅ | `toms::fx`（解析/寫出、`EffectInstance`、`appendQuads`）；`particle_fx_test`（JSON 來回、用種子的確定性、壽命/曲線/力的數學、池上限、爆發時間）；`toms_game --fx=` 預覽和一個 smoke 截圖測試；由 ctest 檢查的 `docs/examples/fx_recipes.particle`，加上像 [16](16_ANIMATION_RECIPES.md) 一樣的食譜章節 | ctest 全綠；火、火花和治療預設在遊戲中正確繪製 |
| **2. 編輯器** ✅ | `particle_editor`（上方的配置）：面板、gizmo、屬性模組、漸層和曲線 widget、預設、時間軸道、`--headless check` / `--render`、`--selftest` | selftest + 截圖；不碰 JSON 就能做出新效果 |
| **2b. 混色模式** | `Quad` 混色 id + 每種組合的 bgfx 狀態、`multiply`/`screen` 的 shader 混合、`.anim` 節點也用同樣的 `blend` 欄位 | 批次依混色切分；每種預設一張基準圖 |
| **3. 動畫整合** | 動畫節點上的 `"effect"`（跟著世界變換、顏色和可見性）、給爆發用的 `fx:` 事件；顯示在動畫編輯器的 viewport 中；戰鬥 `attack` 範例加上打擊火花 | 一個帶火花的戰鬥片段，在兩個編輯器和遊戲中播放結果相同 |
| **4. Studio** | 粒子成為 studio 應用程式的第三個外掛（atlas + 動畫 + 粒子） | 一個應用程式，三個編輯器 |
| **5.（選用）舊格式匯入** | `.prt` / `.prtg` → `.particle`，使用對照表 | 7 個舊檔案都能轉換，在行為是近似的地方發出警告 |

## 8. 決定（2026-10-03）

1. **副檔名：** `.particle`。
2. **混色：** 預設加上任何 src/dest 組合（3a）。等待確認。
3. **順序：** 先做編輯器，再做動畫整合：第 1 階段（編輯器預覽用的
   執行期），然後 2 和 2b，然後 3。
4. **舊格式匯入：** 之後再做。編輯器完成後，提供一個簡單的橋接（`.prt`/`.prtg`，以及 `.pi`/`.mpdi`）。
5. **Viewport 導覽，所有編輯器（atlas、動畫、粒子）：** 右鍵拖曳平移，不只
   中鍵。放在共用的 `CanvasView` 中，粒子編輯器從一開始就加入。
