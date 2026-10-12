# 18 — glTF 3D 模型

> 英文原文：[18_GLTF.md](../18_GLTF.md)

引擎可以載入並繪製 **glTF 2.0** 模型（`.gltf` 加上它的 `.bin` 和圖片，或單一
`.glb`）。包括蒙皮角色、morph target、GPU instancing 和 PBR 材質，在
遊戲執行的每個後端上（Direct3D 11/12、Vulkan、OpenGL）。`gltf_viewer` 是它的測試檢視器，
精神上仿照 [gltf-viewer.donmccurdy.com](https://gltf-viewer.donmccurdy.com/)。

```
Build\windows-release\bin\gltf_viewer.exe tests\gltf\skin_tube.gltf
python tools\gltf_viewer\fetch_samples.py           Khronos sample models -> Build\gltf_samples\
Build\windows-release\bin\gltf_viewer.exe Build\gltf_samples\Fox.glb
```
（`fetch_samples.py` 會把 Khronos 範例模型下載到 `Build\gltf_samples\`。）
在 Visual Studio 中：啟動目標 *gltf_viewer (skinned test model)* 和 *gltf_viewer (Fox sample)*。

## 1. 支援什麼

| 領域 | 支援 |
|---|---|
| 幾何 | 三角形（以及 strip / fan）、線、點；16/32 位元索引或沒有索引；每種 accessor 類型：正規化和量化的整數以浮點數讀取、sparse accessor；沒有法線時會計算（平面）法線 |
| 頂點資料 | position、normal、tangent、2 組 UV、`COLOR_0`、`JOINTS_0` / `WEIGHTS_0` |
| 材質 | metallic-roughness PBR：base colour、metal/rough、normal、occlusion 和 emissive 的貼圖及係數；alpha `OPAQUE` / `MASK` / `BLEND`；雙面；頂點色 |
| 蒙皮 | 關節和 inverse bind matrix，每個 skin **最多 128 個關節**，在 GPU 上蒙皮 |
| Morph target | 任意數量的 target（position、normal、tangent），權重改變時在 CPU 上變形 |
| 動畫 | translation、rotation、scale、morph 權重；`LINEAR`（旋轉：slerp）、`STEP`、`CUBICSPLINE`；可同時播放多個動畫 |
| 場景 | 節點階層（TRS 或矩陣）、預設場景、鏡像的節點（負縮放） |
| 檔案 | `.glb`、`.gltf` + `.bin`、data URI、外部的 PNG / JPEG / BMP / TGA 圖片 |
| 擴充 | `EXT_mesh_gpu_instancing`、`KHR_materials_unlit`、`KHR_texture_transform`、`KHR_materials_emissive_strength`、`KHR_mesh_quantization`、`KHR_materials_pbrSpecularGlossiness`（當作 base colour + roughness）、`KHR_materials_transmission` + `KHR_materials_volume`（玻璃，近似：可以看穿並有反射，但後方的東西不會折射） |

**尚未支援：**
- **OpenGL ES / WebGL 上沒有陰影**（web 版、Android 的 GLES 路徑）。
  - **原因：** Windows 上的 WebGL 透過 Direct3D 上的 ANGLE 執行，它無法連結含有陰影查詢
    （動態索引的陰影矩陣陣列）的 glTF fragment shader。連結失敗時
    log 是空的，bgfx 會停止遊戲；只有在真正的 GPU 上才會失敗，在舊的 web 測試用的 Chrome
    SwiftShader 上不會。
  - **目前的做法：** 那個 shader 版本（`BGFX_SHADER_LANGUAGE_ESSL`）不含陰影程式碼，
    那裡的 `GltfRenderer::shadowsSupported()` 為 false。其餘的光照都相同。
  - **要恢復它們：** 改從一張小的浮點貼圖讀取陰影矩陣（`texelFetch`），
    而不是 uniform 陣列。
- **壓縮過的檔案會被拒絕**，當檔案*需要*以下擴充時，會顯示指名擴充的訊息：
  `KHR_draco_mesh_compression`、`EXT_meshopt_compression`、`KHR_texture_basisu`（KTX2）。要使用
  這種檔案，請在不使用它們的情況下重新匯出，例如用 `gltf-transform`。
- **選用的擴充會列出，但不會繪製：** 例如 clearcoat、sheen 和
  `KHR_lights_punctual`。它們出現在 `Model::ignoredExtensions` 中，在檢視器中以橘色顯示。
- **`KHR_texture_transform`：** 每個材質一個變換，取自 base colour 貼圖（否則
  取自 normal 貼圖），並套用到它所有的貼圖上。
- **還沒有 image-based lighting**（沒有 HDR 環境貼圖）。

已用以下 Khronos 範例檢查：Fox、CesiumMan、RiggedFigure、BrainStem、AnimatedMorphCube、
MorphStressTest、SimpleInstancing、DamagedHelmet、MetalRoughSpheres、AlphaBlendModeTest、
BoxAnimated 和 InterpolationTest（`fetch_samples.py` 會下載它們）。

## 2. 程式碼

| 部分 | 檔案 | 做什麼 |
|---|---|---|
| 載入和姿勢 | `src/core/engine/gltf_model.h/.cpp`（`toms_core`，C++17，不用 GPU） | `loadModel()` → `gltf::Model`（單純的 CPU 資料，透過 [cgltf](https://github.com/jkuhlmann/cgltf)）；`gltf::Pose`：動畫、世界矩陣、關節矩陣、morph 權重、邊界 |
| 繪製 | `src/engine/src/gltf_renderer.h/.cpp`（`toms_bgfx`） | `GltfRenderer`：上傳 `Model`，繪製 `Pose` |
| Shader | `src/engine/shaders/vs_mesh.sc`、`vs_mesh_inst.sc`、`fs_mesh.sc`、`mesh_common.sh`、`vs_line.sc`、`fs_line.sc`、`varying_mesh.def.sc` | 為每個後端編譯並嵌入（`ShaderProgram::Mesh`、`MeshInstanced`、`Line`） |
| 檢視器 | `tools/gltf_viewer/main.cpp` | SDL3 + ImGui 面板 |
| 測試模型 | `tests/gltf/*.gltf`，由 `tools/gltf_viewer/make_test_models.py` 產生 | 自成一體（內嵌的緩衝區和貼圖） |

**在遊戲程式碼中使用它：**
```cpp
gltf::Model model;                              // keep it alive while it is drawn
gltf::loadModel("assets/media/models/hero.glb", model, &error);
GltfRenderer renderer;   renderer.init();       // once, after bgfx starts
auto gpu = renderer.upload(model);              // buffers + textures
gltf::Pose pose;         pose.bind(&model);     // one per character on screen

// every frame
pose.reset();
pose.apply(walk, std::fmod(t, model.animations[walk].duration));   // or blend(i, t, weight)
pose.updateWorld();
GltfRenderer::Frame f;  f.view = view;  f.proj = GltfRenderer::projection(45, aspect, 0.1f, 100);  f.eye = eye;
f.srgbOut = false;       // the desktop backbuffer is sRGB; true on the web
f.lights = {...};        // up to 4 GltfRenderer::Light (directional / point / spot); default: key + fill
f.boundsMin = ...; f.boundsMax = ...;   // what the directional shadow must cover (the scene and its floor)
renderer.begin(viewId, f, width, height);       // uses views viewId .. viewId + GltfRenderer::kViews - 1
renderer.draw(*gpu, pose, placement);           // casts and receives shadows
renderer.draw(*floorGpu, floorPose, floorPlacement, /*castShadows*/ false);
```

（`Model` 在繪製期間要保持存在；`renderer.init()` 在 bgfx 啟動後呼叫一次；每個畫面上的角色一個 `Pose`。
每個畫面：重設姿勢、套用（或混合）動畫、更新世界矩陣；`srgbOut` 桌機為 false、web 為 true；最多 4 盞燈；
`boundsMin/Max` 是平行光陰影要涵蓋的範圍；地板不投射陰影。）

**它如何繪製：**
- 每個圖元**同一種頂點配置**：position、normal、tangent、兩組 UV、顏色、關節、
  權重。缺少的資料用中性值。沒有 tangent 時，shader 會用螢幕空間導數
  建立 tangent frame。
- **蒙皮：** 關節矩陣是 `world(joint) × inverseBind`，放在 `u_joints[128]`
  uniform 中。蒙皮的圖元只用 placement 矩陣繪製，依照 glTF 規格。
- **Morph target：** 只在節點的權重改變時，變形到動態頂點緩衝區中。
  - 限制：同一個 mesh 被兩個節點共用，且同一畫面中權重*不同*時，兩者都會顯示
    最後一個節點的權重。
- **Instancing（`EXT_mesh_gpu_instancing`）：** 每個圖元一次 instanced draw。每個 instance 的
  矩陣是 `world × instance`（蒙皮節點則是 `placement × instance`），放在 bgfx instance data 中。
- **光照：**
  - 最多 4 盞燈：平行光、點光和聚光（GGX / Smith / Schlick）；點光和聚光
    依 1/d² 和它們的範圍衰減（`KHR_lights_punctual` 的視窗函數），聚光在它的
    內外錐角之間衰減；
  - 檔案自己的燈光（`KHR_lights_punctual`）來自 `Pose::lights()`，由它們的
    節點放置；
  - 半球環境光（天空 / 地面），以及有明亮地平線的類攝影棚反射，讓
    金屬看起來像金屬；
  - 解析式的環境 BRDF、曝光，以及 ACES 色調映射。
- **陰影**（每盞燈都可以投射；主光預設開啟）：
  - **一張陰影 atlas**，一張正方形的深度貼圖（`Frame::shadowMapSize`，預設 4096），切成
    圖塊：平行光佔 1 塊，聚光 1 塊，點光 6 塊（立方體每面一個 90° 視角；
    shader 依照往燈光的方向選面）。最多 24 塊；投射的燈越多，圖塊越小。
  - **配合範圍：** 平行光的正交視角涵蓋 `Frame::boundsMin..boundsMax`；
    聚光的視角是它的錐體；點光和聚光的視角延伸到它們的範圍（或邊界的另一側）。
  - **陰影 pass** 用色彩 pass 的 vertex shader（`fs_shadow.sc`），把每個不透明或 mask 的三角形圖元
    畫進每個圖塊，所以蒙皮、變形和 instanced 的 mesh 投射的陰影
    和畫出來的完全一致。混色和玻璃材質不投射陰影。
  - **取樣：** 3×3 次硬體 2×2 PCF（柔和邊緣），保持在圖塊內。
  - **防止陰影痤瘡：** 大約一個陰影 texel 的法線偏移（光線擦過表面時更多，
    也能隱藏低多邊形曲面的稜面）。點光和聚光還會以世界單位
    往燈光方向退一個 texel：深度緩衝區偏移在遠離透視光源時會變得太大。
    `Frame::shadowBias`（檢視器：*shadow bias*，`--shadow-bias=`）同時縮放兩者。
  - **能力：** `GltfRenderer::shadowsSupported()`；沒有可渲染 + 可取樣的深度格式時，
    所有東西都畫成沒有陰影。
- **Alpha：** `MASK` 捨棄低於門檻的部分。`BLEND` 的繪製放到第二個 view，由遠到近
  排序，不寫入深度。玻璃（transmission）也在那裡以預乘方式混色，讓它的
  反射保持明亮。
- **背面：** 雙面材質在背對鏡頭時會翻轉法線。Shader
  不使用 `gl_FrontFacing`：Direct3D 把順時針三角形當「正面」，glTF 把
  逆時針的當正面，所以那個旗標在 D3D 上是反的（曾經讓每個模型都從
  下方打光）。

## 3. 在遊戲中：標題場景

標題畫面在選單後方繪製一個 glTF 場景：預設是 `assets/media/models/VirtualCity.glb`
（`src/game/src/title_scene.*`，由 `GameSession` 擁有）。

- **動畫和鏡頭：** 它循環播放檔案的第一個動畫，並透過
  檔案自己的鏡頭觀看，每 10 秒切到下一個。
  - 每個鏡頭保留它的路徑和注視方向，但地平線保持水平：
    傾斜的鏡頭放在選單後面看起來很怪。
  - 沒有鏡頭的檔案會用緩慢環繞的鏡頭。
- **繪製順序：** `BgfxRenderer::setSceneViews(first, count)` 用 `bgfx::setViewOrder`，把場景的 bgfx view
  （從 `TitleScene::kFirstView` = 16 開始）排在清除畫面（view 0）之後，以及 sprite、RmlUi 和
  ImGui（view 1..3）之前。`count = 0` 時恢復單純的 id 順序。
- **上面的選單：** `title.rml` 的 body 有 `data-class-scene="title.scene"`，而
  `body.scene { background-color: transparent; }` 在場景繪製時取代不透明的 `#414155`。
  場景畫得稍微暗一點（曝光 0.75），讓選單保持可讀。
- **成本：** 桌機上有陰影（2048 atlas）；Android 和 web 上沒有。場景只在
  標題畫面開著時繪製。
- **選擇場景：** `toms_game --title-scene=<file.glb|none>`。沒有檔案或
  載入失敗時，標題和以前一樣是單純的（log 會說明原因）。
- **未驗證：** web 和 Android 版。模型會讓它們打包的資源多 3 MB。

## 4. 檢視器

| 輸入 | 做什麼 |
|---|---|
| 拖放 `.gltf` / `.glb`，或 *Open...* | 載入它，並框住它（涵蓋正在播放的動畫範圍） |
| 左鍵拖曳 / 右鍵或中鍵拖曳 / 滾輪 | 環繞 / 平移 / 縮放 |
| `Space` `F` `G` `K` `B` `W` `R` `H` | 播放-暫停、框住、格線、骨架、邊界、線框、自動旋轉、面板 |
| `1`..`9` | 只播放那個動畫 |
| 方向鍵（按住） | 讓鏡頭在螢幕上往左 / 右 / 上 / 下移動 |
| `C` | 切到檔案的下一個鏡頭（然後回到自由鏡頭） |
| 在檔案鏡頭上：左鍵拖曳 / 滾輪 / 雙擊 | 環顧 / 縮放 / 再看回正前方，仍跟著它的動畫移動 |
| `+` / `-`（或數字鍵盤） | 方向鍵移動加快 / 減慢（每按一次 ×1.5；Display 中的 *move speed* 也可以） |

**面板：**
- **Model：** 數量（頂點、三角形、節點、mesh、材質、貼圖、skin 和關節、
  morph target、instance），以及每個擴充，有繪製的為綠色、被忽略的為橘色，再加上
  警告。
- **Camera**（檔案有鏡頭時）：*free (orbit)* 或檔案的某個鏡頭，依名稱
  （沒有名稱的取它的節點或父節點的名稱）。有動畫的鏡頭節點會移動視角。
  透視鏡頭保留它的視野和 near / far；正交鏡頭保留它的高度。
  **跟著鏡頭走：** 左鍵拖曳環顧（在鏡頭自己的座標系中，繞鏡頭的上方向偏擺、繞右方向俯仰），
  滾輪縮放它的鏡頭；鏡頭持續跟著它節點的動畫。
  雙擊（或 *look ahead*）會再看回正前方。右鍵 / 中鍵拖曳或方向鍵會
  離開，切到自由鏡頭，從檔案鏡頭當時看的位置開始。
- **Animation：** 播放 / 暫停、速度、時間滑桿，以及每個動畫一個核取方塊（可以
  同時播放多個）。
- **Morph targets：** 每個權重一個滑桿，會覆寫動畫對該權重的值。
- **Display：** 格線、座標軸、邊界、骨架、線框、背景、視野，以及以下檢視：
  光照 / 法線 / base colour / metal-rough / UV / 陰影（第一盞有陰影的燈的陰影項）。
- **Lights and shadows：**
  - 陰影開 / 關、*ground*（模型下方承接陰影的地板）、atlas
    大小、陰影偏移；
  - 檔案自己的燈光（`KHR_lights_punctual`），如果有的話；
  - 否則是一組燈光：*default*（主光 + 補光）、*point*、*spot*、*all*，或你自己的。每盞燈有
    類型、顏色、強度、是否投射陰影，然後是方向（偏擺 / 俯仰），或位置、範圍和
    錐角。最多 4 盞；`+ directional` / `+ point` / `+ spot` 新增一盞；
  - 曝光、環境光、天空和地面顏色、色調映射。

  燈光會畫在視圖中：平行光是箭頭，點光是星形，聚光是
  錐體。
- **Performance：** fps、後端、draw call、instance、蒙皮繪製、morph 上傳、陰影圖塊
  和陰影繪製。

**命令列**（測試也會用）：
- `--anim=<index|name|none>`、`--time=<s>`（把動畫凍結在那個時間）；
- `--frames=<n> --screenshot=<png>`；
- `--yaw= --pitch= --zoom=`、`--size=WxH`、`--no-ui`、`--debug=<0..5>`；
- `--camera=<index|name>`（檔案的某個鏡頭）和 `--look=<yaw>,<pitch>`（在它自己的座標系中轉動，單位度）、`--light=default|point|spot|all|file`、`--shadows=0|1`、`--ground=0|1`、`--shadow-bias=<x>`；
- `--renderer=d3d11|d3d12|vulkan|opengl`。

## 5. 測試

- **單元測試 `gltf_model_test`：** 對測試模型做 47 項檢查。
  - **蒙皮：** 靜止姿勢的關節矩陣是單位矩陣；0.25 秒時的 slerp；cubic spline 的 Hermite
    值；CPU 蒙皮後的邊界。
  - **Morph：** 線性和 step 內插的權重；CPU morph 的結果。
  - **Instancing：** 100 個 instance 矩陣和它們的邊界。
  - **材質：** 係數、sRGB、內嵌的 PNG、貼圖變換、emissive strength、
    unlit、alpha 模式。
  - **玻璃和燈光：** 讀取 `KHR_materials_transmission` + volume；由節點放置的 `KHR_lights_punctual` 聚光
    （顏色、強度、範圍、錐角）。
  - **錯誤：** 缺少的檔案；我們無法讀取的必要擴充。
- **截圖測試 `smoke.gltf_*`：** `gltf_viewer` 繪製一個凍結的畫面，和
  `tests/golden/gltf_*.png` 比對。
  - **場景：** 蒙皮（線性和 cubic）、morph（線性和 step）、instancing、PBR、法線
    檢視，以及陰影：平行光（`gltf_shadow_dir`）、點光（`gltf_shadow_point`）、聚光
    （`gltf_shadow_spot`）、三者同時（`gltf_shadow_all`）。每個場景都站在地面上，
    有預設主光的陰影。
  - **後端：** 全部在 Direct3D 11 上；蒙皮、PBR 和全部陰影也在 Vulkan 和 OpenGL 上，
    對同樣的圖片比對。
  - **執行它們：** `ctest --preset windows-release -L gltf`。
  - **刻意變更之後：** `TOMS_UPDATE_GOLDEN=1 ctest ... -L gltf`，然後看過那些
    圖片。
- **修改測試模型：** 編輯 `make_test_models.py`，執行它，然後更新參考圖。

## 6. 群體：用動畫貼圖做 instanced skinning

同一個動畫模型的很多份（怪物、士兵、村民）**每個 primitive 只畫一次**，每一份都有自己的動作片段和時間，還能**在兩個片段之間
混合或交叉淡入淡出**。動畫只烘焙一次，存進一張模型所有副本共用的貼圖，CPU 不用為每一份計算姿勢。

| | `Pose` + `draw()` | 群體：`drawCrowd()` |
|---|---|---|
| 每份每幀的 CPU 工作 | 取樣每個 channel、世界矩陣和關節矩陣 | 一筆 80 位元組的紀錄（位置、哪些片段、時間） |
| 每份每幀上傳 | 最多 128 個關節矩陣（8 KB） | 80 位元組 |
| Draw call | 每份每個 primitive 一次 | 所有副本每個 primitive 一次 |
| 動畫 | 精確，任意多個動畫混合，morph target | 以 `fps`（預設 30）烘焙，幀之間 nlerp；一個片段，或兩個混合 |
| 適合 | 主角、首領、臉部 | 其他所有角色 |

**程式碼**

| 部分 | 檔案 |
|---|---|
| 烘焙、片段表、存檔／讀檔、每份的動畫狀態（`Crowd`） | `src/core/engine/gltf_anim_texture.h/.cpp`（只用 CPU；有單元測試） |
| 上傳、instanced 繪製 | `GltfRenderer::uploadAnimTexture`、`drawCrowd`、`crowdsSupported` |
| 頂點著色器 | `src/engine/shaders/vs_mesh_animtex.sc`（+ `fs_mesh.sc`、`fs_shadow.sc`：`ShaderProgram::MeshAnimTex`、`MeshShadowAnimTex`） |
| 檢視器 | `gltf_viewer --crowd=<n>` 和面板的 *Crowd* 區 |

```cpp
// 每個模型一次
auto anim = std::make_shared<gltf::AnimTexture>();
gltf::bakeAnimTexture(model, /*skin*/ 0, /*fps*/ 30.0f, *anim);       // 24 個關節、3 個片段約 1 毫秒
auto animGpu = renderer.uploadAnimTexture(anim);                     // 這個模型的所有群體共用

gltf::Crowd crowd(anim);
int i = crowd.add(placement, anim->clipIndex("Walk"));
crowd.crossFade(i, anim->clipIndex("Run"), 0.4f);                     // 0.4 秒淡入淡出，之後只播 Run
crowd.setBlend(j, walk, run, 0.3f);                                   // 維持 70% 走 + 30% 跑
crowd.play(k, anim->clipIndex("Survey"), /*loop*/ false);

// 每幀
crowd.update(dt);
renderer.drawCrowd(*gpu, *animGpu, crowd);                            // 在 begin() 和這一幀結束之間
```

**貼圖。** RGBA32F；**每個烘焙的幀一列**，所有片段的幀依序疊放；**每個關節 2 個 texel**：

| Texel | 內容 |
|---|---|
| `2j` | 關節的旋轉，四元數（x, y, z, w） |
| `2j + 1` | 平移（x, y, z）和等比縮放（w） |

內容是 `world(joint) × inverseBind`，和 `Pose::jointMatrices` 給的完全相同。片段的第 `f` 幀是 `min(f / fps, duration)`
時的姿勢，所以片段有 `ceil(duration × fps) + 1` 列，最後一列是結束姿勢。大小：關節 × 2 × 列 × 16 位元組，例如 Fox（24 個關節、
3 個片段、163 列）是 122 KB；128 個關節、30 fps 共 10 秒的片段約 1.2 MB。列數受 GPU 限制（WebGL2 保證 2048–4096：30 fps
約 1–2 分鐘的片段）；超過限制的貼圖會被拒絕並顯示訊息（降低 fps 再烘焙）。

**為什麼存旋轉 + 平移，而不是矩陣。** 兩個片段在每個關節上混合：旋轉用 nlerp（對齊正負號，走短的那一邊），平移和縮放線性混合。
兩個關節矩陣直接平均時，姿勢差很多（手臂向前對向後）會讓肢體縮短或變扁；四元數不會。每個關節也只要 2 個 texel 而不是 3 個。
這個格式的限制：
- **非等比的關節縮放** 會被平均：`AnimTexture::maxScaleError` 回報差多少（一般骨架為 0）。
- **鏡像關節**（負縮放）無法儲存：`mirroredJoints` 會標示。

**每一份**（bgfx instance data，80 位元組，`CrowdGpuInstance`）：

| | 內容 |
|---|---|
| `i_data0..2` | 擺放矩陣的前三列（第四列是 0, 0, 0, 1） |
| `i_data3` | 片段 A：這一幀的列、下一幀的列、兩者之間的比例 |
| `i_data4` | 片段 B：同上，`w` 是 B 的權重（0：完全不讀 B） |

幀的列和比例在 CPU 上算好（`gltf::framePair`：循環的會繞回，只播一次的停在最後一幀），著色器不處理時間。

**著色器。** 對頂點的四個關節（權重 0 的跳過）讀片段 A 的兩幀（4 個 texel），只有在混合時才讀片段 B 的兩幀（再 4 個）；
nlerp；組出關節矩陣；接著線性混合蒙皮，光照和其他網格相同。陰影使用同一個頂點著色器，所以也會動。

**群體不畫的東西：** 其他 skin 的或沒有蒙皮的 primitive（用 `Pose` 畫）；morph target（不烘焙）。一張貼圖一個 skin：
有多個 skin 的模型每個 skin 要一張貼圖。

**存檔／讀檔**（`saveAnimTexture` / `loadAnimTexture`，`.animtex`）。檔案存有 magic 和版本、關節和列數、fps、片段表和原始的
浮點數。也存有 `modelHash()`，涵蓋 skin（關節名稱、inverse bind 矩陣）和動畫（名稱、長度、channel 數）：用烘焙後已改變的
模型讀取會以 *stale* 失敗。烘焙只要幾毫秒，所以載入時直接烘焙也可以。

**和舊引擎的 `cAnimTexture`（FM79979 `AnimationTexture.cpp`）比較：** 概念相同（烘焙、每個 instance 的幀、在著色器中混合
兩幀），差別如下：
- 所有片段共用一張貼圖，一次繪製可以混用不同片段和時間；
- 可以混合兩個片段；
- 用 instance data 而不是 uniform 陣列，所以沒有 instance 數量上限；
- 第 0 幀就是 t = 0（舊的烘焙晚了一步）；
- 檔案記錄它是從什麼烘焙的，所以能發現過期的快取。

**驗證：**
- 群體中的一份和用 `Pose` 畫同一片段的結果逐像素相同（`smoke.gltf_crowd_one` 使用 `gltf_skin` 的參考圖）；
- Fox 群體在 D3D11、D3D12、Vulkan 和 OpenGL 之間一致；
- `gltf_anim_texture_test` 把烘焙結果和 `Pose::jointMatrices` 比對（在幀的時間點完全相同，幀之間差距在 0.01 以內），並檢查混合、
  群體狀態和存檔／讀檔。
