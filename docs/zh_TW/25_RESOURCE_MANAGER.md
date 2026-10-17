# 25 — 資源管理器與從 JSON 產生物件（設計）

> 英文原文：[25_RESOURCE_MANAGER.md](../25_RESOURCE_MANAGER.md)

這是一份設計：核心引擎裡只有一個資源管理器，而算圖物件與遊戲物件都用同一種方式從 JSON 產生。
它從 FM79979 已經做過的出發，看 TOMS 現在的做法，最後給出分成小步驟的計畫。

狀態（2026-10-17）：計畫（第 6 節）的第 1–3 步**已完成**：資源快取、遊戲改由它載入、從 JSON 產生算圖物件。
第 4–6 步尚未進行。擁有者的決定在第 8 節；第 9 節列出完成的內容與測試。

## 1. 摘要

| TOMS 現在 | 提議 |
|---|---|
| 每個使用者各自載入一份圖集與它的貼圖：待機動畫、開門動畫、每個物件的動畫檔、兩種預覽、每個編輯器畫布 | **依檔案路徑的單一快取**。一個圖集只載入一次，以 `std::shared_ptr` 共用 |
| 貼圖只能由載入它的程式碼釋放（`Game` 裡的各個 `releaseTexture` 清單） | 持有是自動的（`shared_ptr`）。沒人用的資源留在快取，直到超過**記憶體預算**；**戳記**（範圍）釋放預覽或文件載入的東西 |
| 物件的外觀是一個 JSON 物件，**每一幀都重新合併、重新讀取**（`Game::lookOf`） | JSON **只轉換一次**成算圖物件（載入樓層時），每一幀只負責畫 |
| 每種繪圖各有一條程式路徑（圖片、動畫、待機動畫、粒子預覽、glTF 檢視器） | 一個基底類別 `RenderObject`，由**工廠依 `"type"` 欄位**產生；每個實際類別自己註冊，就像 `GetMe` |
| 遊戲物件是普通的 struct，由程式從 `enemies.json`、`items.json` 與關卡檔逐欄填入 | 同一種工廠模式依資料產生遊戲物件 |

結果：
- 每頁圖集在 GPU 上只有一份；
- 繪圖時不處理 JSON；
- 樓層會釋放只有它用過的東西；
- 編輯器與遊戲共用同一套載入程式；
- 新增一種物件只要一個類別加一行註冊。

## 2. FM79979 的做法（參考程式）

### 2.1 `NamedTypedObject`：共同基底

`Core/Common/NamedTypedObject.h`

- **型別：**每個類別都有一個 `TypeID`（`DEFINE_TYPE_INFO`、`TYPDE_DEFINE_MARCO`）。`Type()` 是虛擬函式，
  所以從基底指標就能知道實際類別。`IsSameType` 只比較指標，很快。
- **名稱**（`wstring`）、**擁有者**（`m_pWoner`）與**唯一 ID**（`GetGlobalUniqueID`）。
- **`Clone()`**（`CLONE_MYSELF`、`LAZY_CLONE_FUNCTION`）：載入一次的物件當作範本，場景裡用的是複製品。
- **`ToTiXmlElement()`：**物件能把自己寫回去。
- **除錯：**`DumpUnReleaseInfo()` 列出從未刪除的物件，magic ID 能抓到已刪除的指標。

### 2.2 `cGameResourceManager`：每種資源一個管理器

`Core/GameplayUT/ResourceMaintaner/GameResourceManager.h`

- 有一個依 `eGameObjectType` 編號的管理器陣列：
  - 2D 網格、形變動畫、MPDI 清單、音效、圖片（PI）、粒子、行為粒子、字型；
  - 還有 `eGBT_DEFAULT_RESOURCE_PARSER`，放不是檔案的物件。
- 查詢方式是 `GetObject<T>(name, type)` 與 `GetObjectByFileName<T>(file, type)`，每次都用 `dynamic_cast`
  轉成實際類別。
- **`ParseLazyResource(file)`：**一個 XML 檔列出現成的算圖物件，例如
  `<cNumeralImage PI="CarGoApp/JP_BBQ/UI.pi" Image0="ID-0" … Name="CarIDNumerial"/>`。
  - 每個元素經過 `GetRenderObjectBehavior(element)`（`ObjectXMLParser.cpp`），它拿標籤跟每個類別的 `TypeID`
    比對，再呼叫那個類別的 `GetMe(element)`。
  - 結果依名稱保存。

### 2.3 `GetMe` 與 `cCommonRegisterManager`：工廠

`Core/GameplayUT/CommonRegister/CommonRegisterManager.h`

- 每個類別都有 `static Base* GetMe(TiXmlElement*)`。
  - 元素的標籤不是自己的型別時，它回傳 `nullptr`。
  - 否則回傳 `new Type(element)`。
  - `GET_ME_WITH_BASE_TYPE` 與 `LAZY_GET_ME` 會產生這個函式。
- 管理器保存這些函式的清單（`REGISTER_CLONE_FUNCTION_TO_MANGER`）。
  `GetObjectByParseXmlElement` 逐一嘗試，回傳第一個產生出來的物件。
- 呼叫端只知道基底型別，實際類別由資料決定。
- 圖片的例子：`cPuzzleImageUnit::GetMe` 讀取：
  - `PI`（圖集檔，經 `cGameApp::GetPuzzleImageByFileName` 找到或載入）；
  - `PIUnit`（圖片在那個圖集裡的名稱）；
  - `Name`。

  它回傳那個圖片單元或它的複製品。這正是「一個 JSON 物件說明自己是圖片、指出圖集與圖片名稱，然後拿回一張圖片」。

### 2.4 `cResourceStamp`：釋放一個場景載入的東西

`Core/GameplayUT/ResourceMaintaner/ResourceStamp.h/.cpp`

1. `StampResource1()` 複製每個管理器目前的清單：動畫、圖片、音效、粒子、字型、Collada、碰撞。
2. 場景載入它需要的東西。
3. `StampResource2()` 再複製一次清單。
4. `ReleaseNewResource()`：
   - 每個管理器中，第二份裡超出第一份數量的項目就是「新的」；
   - 把它們從管理器移除，於是被刪除；
   - 「如果想保留某些新資料，就把它從 m_Stamp2Data 移除。」

只會釋放那個場景新增的東西，原本就在的會留下。

### 2.5 `cSmartObject`：參考計數

`Core/Common/SmartPointer.h`

- `AddRef(user)` 與 `Release(user)` 計算使用者數量，歸零時物件自己刪除。
- 除錯版會保存使用者清單。

### 2.6 保留什麼、改變什麼

| 保留 | 改變 |
|---|---|
| 每個類別一個型別名稱，從基底指標就能讀到 | `wchar_t` 名稱 → 全部用 UTF-8 `std::string`；每次查詢都 `dynamic_cast` |
| 資料檔描述物件 | XML（TinyXML）→ JSON（nlohmann），TOMS 每個工具本來就寫 JSON |
| 載入一份、多個使用者；範本加複製品 | 原生指標加手動 `AddRef`/`Release` → `std::shared_ptr` / `std::weak_ptr` |
| 工廠：資料指名類別，由類別自己產生自己 | 逐一嘗試的一串 `GetMe` → 從型別名稱對到建立函式的 map（查一次） |
| 圖片 = 圖集檔 + 圖片名稱 | 依 enum 編號、每種一個管理器 → 以（型別、路徑）為鍵的單一快取 |
| 釋放一個範圍載入的東西（戳記） | 三份快照加數量運算 → 每個資源記得載入它的戳記 |
| 列出從未釋放的物件 | （TOMS 已經有：`ObjectRegistry::DumpLeaks`） |

## 3. TOMS 現在有什麼

### 3.1 基底類別已經存在

[src/core/engine/object.h](../../src/core/engine/object.h) 移植了 `NamedTypedObject`：
- `Trackable` 帶有型別、名稱、唯一 ID 與存活物件登錄表。
- `Object` 加上 `shared_ptr` 與 `As<T>()`。
- `TOMS_OBJECT(T)` 提供 `StaticType()` 與 `Type()`。

[node.h](../../src/core/engine/node.h) 與 [scene.h](../../src/core/engine/scene.h) 提供場景樹：`Node`，然後是
有 `Render(IRenderer*)` 的 `GameObject`，再來是 `SpriteNode` 與 `TextNode`。

**缺少的：**資源型別、快取、工廠與戳記。

### 3.2 目前資產怎麼載入

| 資產 | 在哪裡 | 怎麼保存 |
|---|---|---|
| 遊戲的圖片圖集 | `Game::spriteAtlas_`、`BgfxRenderer::loadSpriteAtlas` | 一份，屬於 `Game`；貼圖借給 UI（[shared_textures.h](../../src/engine/src/shared_textures.h)） |
| 其他圖集（動畫、粒子） | `Game::loadPreviewAtlases`（[game_scene_draw.cpp](../../src/core/game/core/game_scene_draw.cpp)） | **每個呼叫者一份新的**：`idleAtlases_`、`doorAtlases_`、`objAnims_[file]->atlases`、`previewAtlases_`、`previewFxAtlases_`，各自用 `ren->loadTexture` 上傳頁面 |
| `.anim` 檔 | `idleAnim_`、`doorAnim_`、`objAnims_`（依檔案）、`previewAnim_` | 每種用途一個成員 |
| `.prt` 粒子檔 | `previewFxFile_` | 只有預覽；圖集又經過一次 `loadPreviewAtlases` |
| glTF 模型 | `gltf::loadModel` 與 `GltfRenderer::upload`（[gltf_renderer.h](../../src/engine/src/gltf_renderer.h)） | 呼叫者自己擁有 `Model` 與 `GpuModel` |
| 物件外觀 | `Entity::look`、`Game::typeLook` / `lookOf` | JSON；**每次繪製都重新合併**（`lookOf` 每幀為每個實體建立新的 `json`） |
| 編輯器 | `GameTextures`（[GameCanvasView.h](../../tools/studio_common/GameCanvasView.h)）、`StageCanvas::m_animTex`、`SpriteImageCache` | 每個畫布、每個 `.anim` 檔各一份 |

### 3.3 因此產生的問題

1. **同一個圖集在記憶體與 GPU 上有好幾份。**如果待機動畫、開門動畫和某個物件自己的動畫都用 `fx.atlas`，
   它的頁面會上傳三次。
2. **誰釋放什麼分散在各處。**每個 `std::vector<uint16_t> …Textures_` 都要在清空前手動釋放。漏掉一條路徑
   就會洩漏 GPU 記憶體。
3. **每一幀都處理 JSON。**合併外觀、找欄位、解析 `"file#clip"`，每幀對每個實體做一次。在關卡編輯器裡，
   同樣的模式讓 Debug 版每次重新整理要花約 1 秒（編輯器已於 2026-10-17 修正）。
4. **沒有範圍。**玩家離開樓層時，沒有東西會釋放那層的資產。現在沒出問題只是因為遊戲還小。
5. **每種新的可繪物都是 `Game` 裡的新程式碼。**在地圖上加粒子、或在選單裡放 3D 模型，就要新的成員、
   新的載入程式和新的釋放程式。

## 4. 設計

以下全部放在 `src/core/engine/`（命名空間 `toms::res`）。介面裡沒有 Qt 也沒有 bgfx，所以遊戲、網頁版、
Android 與 Qt 編輯器都能用。

### 4.1 資源的鍵

資源以 **（型別、正規化路徑）** 尋找。

- 路徑的正規化方式和 `sharedTextureKey` 一樣：`/` 分隔、摺疊 `..`、轉小寫。這樣 Windows 與 VFS 的拼法就會一致。
- 檔案的一部分（圖集裡的一張圖、`.anim` 檔裡的一段動畫、`.prt` 檔裡的一個特效）**不是**資源，而是*指向*
  資源的參照：資源加上一個名稱。

### 4.2 `Resource`：所有載入物的基底

```cpp
// resource.h
class Resource : public Object {                 // 扮演 NamedTypedObject 的角色：型別、名稱（= 路徑）、唯一 ID
    TOMS_OBJECT(Resource)
public:
    const std::string& path() const;             // 正規化後的鍵
    int stamp() const;                           // 載入它的範圍（4.5）
    int version() const;                         // 重新載入時 +1（4.6）
    int contentVersion() const;                  // version() 加上它依賴的東西（圖集的頁面）
    size_t bytes() const;                        // CPU + GPU 大小，給記憶體報表用
    enum class State { Ready, Failed } state() const;
    const std::string& error() const;            // 失敗原因（只記錄一次並保存，不會每幀重試）
protected:
    virtual bool load(ResourceManager& rm, std::string& error) = 0;   // 類別自己讀取自己（如同 GetMe 自己產生自己）
};
```

以下是實際類別。每個都是包住引擎現有型別的小外殼：

| 類別 | 內容 | 依賴 |
|---|---|---|
| `TextureRes` | GPU 貼圖代號（`IRenderer::loadTexture`）與大小；解構時釋放 | — |
| `AtlasRes` | `toms::AtlasFile` + 每頁一個 `shared_ptr<TextureRes>`；`find(name)` | `TextureRes`（它的頁面） |
| `AnimRes` | `toms::anim::AnimFile` + 由它的圖集組成的 `AtlasSet` | `AtlasRes` |
| `ParticleRes` | `toms::fx::ParticleFile` + 它的 `AtlasSet` | `AtlasRes` |
| `ModelRes` | `gltf::Model` + `GpuModel`（以及烘焙好的群眾動畫貼圖） | — |
| `FontRes` | `BitmapFont` 與它的頁面貼圖 | `TextureRes` |
| `SoundRes` | 解碼後的音訊，或串流描述 | — |
| `JsonRes` | 解析好的資料檔（`enemies.json`、關卡檔） | — |

**依賴就是 `shared_ptr`。**`AtlasRes` 讓它的頁面貼圖保持存活，`AnimRes` 讓它的圖集保持存活。釋放動畫檔時，
只有在沒有別人使用的情況下才會一起釋放圖集。遊戲自己的 `game.atlas` 也只是一個普通的 `AtlasRes`。
「把貼圖借給 UI」的登錄表變成「UI 向管理器要同一個 `TextureRes`」。

### 4.3 `ResourceManager`：依路徑的單一快取

```cpp
class ResourceManager {
public:
    explicit ResourceManager(IRenderer* ren);    // nullptr：只有 CPU（測試、用 QPainter 畫的工具）
    template <class T> std::shared_ptr<T> get(const std::string& path);   // 有快取就拿，沒有就現在載入；絕不為 null
    template <class T> std::shared_ptr<T> find(const std::string& path) const;   // 只找快取
    template <class T> std::shared_ptr<T> provide(const std::string& path, std::function<void(T&)> update);   // 由擁有者產生
    void setPathMapper(std::function<std::string(const std::string&)> mapper);   // 例如所有 game.atlas -> @sprite-atlas

    void setBudget(size_t bytes);                // 4.4：沒人用的資源保留到超過這個大小
    size_t releaseUnused();

    int  pushStamp(const std::string& label);    // 4.5
    void popStamp(int stamp);
    void keep(const std::shared_ptr<Resource>& r);   // 撐過目前的戳記（「把它從 m_Stamp2Data 移除」）

    int  checkFiles();                           // 4.6：重新載入磁碟上改變的檔案
    bool refresh(const std::shared_ptr<Resource>& r);
    void detachRenderer();                       // 算圖器先離開：立刻釋放所有貼圖
    std::vector<Info> report() const;            // 路徑、型別、大小、使用者數、戳記：洩漏／記憶體清單
};
```

- **快取**是 `std::unordered_map<Key, Entry>`。每個項目保存 `shared_ptr<Resource>`（快取自己的參照）、載入它的戳記、
  最後一次被要求的時間，以及檔案的時間與大小。鍵是型別加上轉成絕對路徑並正規化的路徑，所以 `media/atlas/fx.atlas` 和
  `D:/…/assets/media/atlas/fx.atlas` 是同一個圖集。
- **`get<T>`** 對同一路徑每次都回傳同一個指標。第一次呼叫時經 `vfs.h` 讀檔（所以桌面、網頁與 Android 都能用），
  然後建立 `T`。載入失敗也會快取成 `Failed` 並保存錯誤，所以只回報一次，不會每幀回報。
- **每個類別自己載入自己**（`Resource::load`），就像 FM79979 每個類別在 `GetMe` 裡自己產生自己。重新載入時對同一個
  物件呼叫同一個函式。圖集的 `load` 對每一頁呼叫 `get<TextureRes>`，所以頁面也是共用的。
- **執行緒：**`get` 在主執行緒執行，因為 GPU 上傳必須在那裡。之後可以加 `prefetch(paths)`：在 `job_system.h`
  的工作執行緒讀檔、解碼，再回主執行緒上傳（[10_THREADS.md](10_THREADS.md)）。
- 每個行程一個管理器。遊戲擁有一個；每個編輯器程式擁有一個，讓它所有的畫布共用。它以參考傳遞，
  不做成全域變數，所以測試可以自己建立。

### 4.3a 資源清單：遊戲需要的東西列在一處，用同一種方式取得（2026-10-17）

[assets/media/resources.json](../../assets/media/resources.json) 列出遊戲需要的檔案，每個都有一個鍵：

```json
{ "resources": {
    "atlas.fx":          { "type": "atlas", "path": "atlas/fx.atlas", "preload": true },
    "anim.monster_idle": { "type": "anim",  "path": "anim/monster_idle.anim", "preload": true },
    "anim.door_open":    { "type": "anim",  "path": "anim/door_open.anim", "preload": true } } }
```

**啟動時：**遊戲設定根資料夾（`setRoot`：`assets/media`）、讀取清單（`loadManifest`），並在圖片圖集就緒後載入所有
`preload` 項目（`preload`）。

**取得任何資源的同一種方式：**`res->get<T>(name)`，`name` 可以是：
- **清單的鍵**（`"anim.door_open"`），或
- **根資料夾下的路徑**（`"anim/door_open.anim"`，資料檔裡的寫法）。鍵與它的路徑得到同一個物件。

還沒載入的就現在載入。程式不再為每個功能寫載入函式（`loadIdleAnims` 與 `loadDoorAnims` 已移除；地圖物件直接要
`"anim.monster_idle"` 與 `"anim.door_open"`）。

**缺少或損壞的檔案：**
- `get` 回傳 Failed 資源，絕不為 null；
- 每個檔案只通知缺檔處理函式（`setOnMissing`）一次；
- 遊戲記錄下來，並在地圖出現後顯示**「找不到檔案：<鍵或路徑>」**（文字鍵 `res.missing`）；
- 遊戲照常進行（沒有待機動畫的怪物就站著不動）。

用錯型別要求某個鍵（把圖集當動畫）也以同樣方式回報。

**檢查：**`unit.resource_test` 會載入出貨的清單，任何項目缺少、無法載入或型別未知都會失敗。修改 `resources.json`
會選到這個測試。

### 4.4 持有資源：AddRef 與 Release 自動發生

`cSmartObject` 需要手動 `AddRef(user)` 與 `Release(user)`；少一次 `Release` 就洩漏，多一次就刪掉還在用的物件。
這裡的計數由 `std::shared_ptr` 負責，兩個呼叫都由語言自動完成：

| FM79979 | TOMS |
|---|---|
| `p->AddRef(this)` | 複製指標：`auto mine = atlas;`（或存成成員） |
| `p->Release(this)` | 放手：成員被銷毀、區域變數離開範圍，或 `mine.reset()` |
| `GetReferenceCount()` | `use_count()`；`ResourceManager::report()` 列出每個資源的持有者數 |
| 除錯用的使用者清單 | `report()` + 結束時的 `ObjectRegistry::DumpLeaks`（「live objects = 0, no leaks」） |

- **不用呼叫、也不會忘記。**一個類別只要有 `shared_ptr` 成員，就剛好在它存活期間持有資源，包括被複製或因例外而銷毀時。
- **所有資源都由管理器產生**（`get<T>` 用 `std::make_shared`），所以永遠不會有需要配對 `delete` 的原生 `new`。
  只要還有人持有，資源就不會被刪除。
- **依賴也用同樣方式互相持有：**圖集持有它的頁面貼圖，動畫檔持有它的圖集。
- **貼圖與算圖器：**`TextureRes` 在解構時經由共用的 `GpuLink` 釋放 GPU 貼圖。算圖器先離開時，`detachRenderer()`
  一次釋放所有貼圖並清掉連結，之後才釋放的貼圖絕不會呼叫已經不存在的算圖器。
- 只查詢、不保存的程式碼用 `find<T>`（不會載入）。

**為什麼用 `shared_ptr`，而不是物件內的計數？**`Object` 已經使用 `shared_ptr`（搭配 `enable_shared_from_this` 與
`As<T>()`），標準型別能放進任何容器、也能配 `weak_ptr`，而資源層也沒有熱到計數成本會有影響：指標是在載入時複製，
不是每一幀。

**記憶體：用預算，而不是每層樓釋放（擁有者的問題 2）。**目前整個遊戲的圖片解碼後約 2 MB，所以玩家離開樓層時釋放那層的
美術不會省下什麼，回來時還要重新載入。改成：
- 沒人持有的資源**留在快取**（剛離開的樓層仍然載入著）；
- 只有當沒人用的資源合計超過**預算**（`setBudget`，預設 64 MB）時，才從最久沒被要求的開始釋放，直到回到預算內。
  網頁或手機版可以設較小的預算；
- `releaseUnused()` 一次釋放所有沒人用的（收到記憶體不足警告，或換美術風格之後）；
- 戳記（4.5）保留給明顯會來來去去的東西，例如預覽或編輯器文件。

以目前的資產永遠不會碰到預算，所以不會重新載入任何東西。遊戲變大後（更多美術風格、3D 模型），預算就會開始作用，
不需要改程式。

### 4.5 戳記：釋放一個範圍載入的東西

這就是 `cResourceStamp`，只是變成自動的。

```cpp
int s = res.pushStamp("floor F03");   // 從現在起第一次載入的資源都記得 s
loadFloor("F03");                     // 建立這層的物件：它們用 get<> 取得需要的東西
...
res.popStamp(s);                      // 玩家離開 F03：F03 載入的東西從快取中移除，
                                      // 除非還有東西持有它，或呼叫過 keep()
```

- 每個項目記得**第一次**載入它的戳記。在這個戳記之前載入的東西（標題畫面的 UI 圖集、`game.atlas`）屬於較舊的戳記，
  這個戳記永遠不會釋放它們。這和「戳記 1 = 舊的、戳記 2 = 舊的 + 新的、只釋放新的」一致。
- `popStamp` 放掉快取對該戳記每個資源的參照：
  - 沒有別人持有就立刻釋放；
  - 還有人持有（例如帶到下一層的物件）就移到外層的戳記。
- `ResourceStamp` 是一個小型 RAII 型別，建構時 push、解構時 pop，用於載入畫面與編輯器預覽。
- **用在哪裡：**預覽、編輯器文件、載入畫面：在某個時間點明確結束的東西。樓層與戰鬥不用戳記，由預算（4.4）處理。
- 除錯版中，`popStamp` 會記錄它釋放了什麼、保留了什麼，以及原因。

### 4.6 檔案版本：重新載入改變的東西

「戳記」還有第二個意思，給工具用：檔案自己的時間與大小。

- `checkFiles()` 拿每個已快取檔案的修改時間與大小跟讀取時比較。
  - 熱重載開啟時，遊戲每秒執行一次：`toms_editor`（Qt 視窗裡的遊戲）永遠開啟，桌面除錯版預設開啟，正式版用
    `--hot-reload` 開啟。發行的遊戲從不檢查（擁有者的決定 3）。
  - 有變化時，資源**原地**重新載入：同一個物件、新的內容，`version()` 增加。
- 保存衍生資料的使用者（動畫播放器的姿勢快取、圖片的區域指標）拿自己看過的版本跟 `version()` 比較，再重新查詢。
  - 這取代 `lentTexturesVersion()`、`GameTextures::cached` 的 `cacheKey`，以及各編輯器自己的監看程式。
- 重新載入失敗時保留舊內容並回報錯誤，所以存到一半的檔案絕不會讓畫面變空白。

### 4.7 從 JSON 產生算圖物件：`GetMe` 工廠

**JSON** 說明要畫什麼。`"type"` 選擇類別，其他欄位屬於那個類別。（路徑僅為範例。）

```json
{ "type": "sprite",   "atlas": "media/atlas/game.atlas", "name": "slime", "tint": [1, 0.8, 0.8, 1] }
{ "type": "clip",     "anim": "media/anim/monster_idle.anim", "clip": "idle_slime", "loop": true }
{ "type": "particle", "file": "media/fx/fx.prt", "effect": "spark", "loop": true }
{ "type": "model",    "file": "media/models/VirtualCity.glb", "scale": 0.5 }
{ "type": "text",     "font": "media/fonts/score.fnt", "text": "1200" }
{ "type": "group",    "children": [ { "type": "sprite", "…": "…" }, { "type": "particle", "…": "…" } ] }
```

- `"atlas"` + `"name"` 就是 FM79979 的 `PI` + `PIUnit`：圖集是擁有者，名稱是其中的圖片。
- 簡寫依然有效：`"name": "fx:spark"` 用圖集 id，就像 `.anim` 的圖片鍵已經在做的（`parseSpriteRef`）。
- 關卡編輯器現有的 `look`（`{"sprite", "anim": "file#clip", "tint"}`）由一個小轉接器讀成 `clip` 或 `sprite`。
  已存的關卡不必修改。

**基底類別**是 `GameObject`，所以它在場景樹裡、有變換、可以隱藏，也會畫它的子物件：

```cpp
class RenderObject : public GameObject {
    TOMS_OBJECT(RenderObject)
public:
    virtual void update(float dt) {}                       // 動畫與粒子前進
    virtual std::shared_ptr<RenderObject> clone() const = 0;   // 範本的實例（FM79979 Clone）
    virtual nlohmann::json toJson() const = 0;             // 把自己寫回去（FM79979 ToTiXmlElement）
};
class SpriteObject   : public RenderObject { std::shared_ptr<res::AtlasRes> atlas; const AtlasRegion* region; float tint[4]; … };
class ClipObject     : public RenderObject { std::shared_ptr<res::AnimRes> anim; const anim::Clip* clip; float time, phase; … };
class ParticleObject : public RenderObject { std::shared_ptr<res::ParticleRes> file; fx::EffectInstance fx; … };
class ModelObject    : public RenderObject { std::shared_ptr<res::ModelRes> model; gltf::Pose pose; … };
class TextObject     : public RenderObject { std::shared_ptr<res::FontRes> font; std::string text; … };
class GroupObject    : public RenderObject { /* 只有子物件 */ };
```

**工廠**就是 `cCommonRegisterManager`，只是用 map 取代逐一嘗試。改了什麼、為什麼比 `GetMe` 好：

| `GetMe` | `RenderObjectFactory` |
|---|---|
| 逐一嘗試每個類別的 `GetMe`，直到有一個說「是我的」：類別越多越慢 | 依 `"type"` 查一次 map |
| 每個 `GetMe` 自己比對標籤，某個類別打錯字就默默永遠比對不到 | 型別名稱只寫一次（`kTypeName`）；類別缺少 `kTypeName` 或 `fromJson` 時，`add<T>()` 會編譯失敗 |
| 回傳 `nullptr`，沒有原因 | 回傳原因與它在樹中的位置（`children[1]: no sprite 'x' in media/atlas/fx.atlas`）；`makeOrMissing` 改畫一個紅框 |
| 原生 `new`，由記得的人釋放 | `shared_ptr` |
| 每個類別手寫複製 | 每個類別都有 `clone()`，再加上 `instance()`：每種描述一個範本，每次使用只做便宜的複製 |

註冊是明確的清單（`registerBuiltinRenderObjects`），而不是會自己註冊的靜態物件。靜態函式庫裡的靜態註冊物件，
如果沒有別的程式參照它所在的檔案，就會被連結器丟掉，那個類別便默默從工廠消失。

```cpp
using MakeFn = std::function<std::shared_ptr<RenderObject>(const nlohmann::json&, res::ResourceManager&, std::string* error)>;

class RenderObjectFactory {
public:
    void add(const std::string& type, MakeFn fn);          // REGISTER_CLONE_FUNCTION_TO_MANGER
    std::shared_ptr<RenderObject> make(const nlohmann::json& j, res::ResourceManager& res, std::string* error = nullptr) const;
};

// 每個類別，如同 GET_ME_WITH_BASE_TYPE：
std::shared_ptr<RenderObject> SpriteObject::fromJson(const nlohmann::json& j, res::ResourceManager& res, std::string* error) {
    auto atlas = res.get<res::AtlasRes>(j.value("atlas", kGameAtlas));
    const AtlasRegion* r = atlas->find(j.value("name", ""));
    if (!r) { if (error) *error = "no sprite '" + j.value("name", "") + "' in " + atlas->path(); return nullptr; }
    auto o = std::make_shared<SpriteObject>(atlas, r);
    readTint(j, o->tint);
    return o;
}

// 內建類別集中在一處列出（CommonRegisterManager 的 AddCloneRegisterFunction）：
void registerRenderObjects(RenderObjectFactory& f) {
    f.add("sprite", &SpriteObject::fromJson);
    f.add("clip", &ClipObject::fromJson);
    f.add("particle", &ParticleObject::fromJson);
    f.add("model", &ModelObject::fromJson);
    f.add("text", &TextObject::fromJson);
    f.add("group", &GroupObject::fromJson);
}
```

- **未知的型別，或找不到檔案或圖片，**會得到 `nullptr` 和錯誤文字。呼叫端記錄它並畫出明顯的「缺少」標記，
  所以資料錯誤看得見，而不是當機。FM79979 用 `"not registered element"` 做的是同一件事。
- **範本與複製品：**被許多格子使用的 JSON 描述（例如 `monster:slime` 的類型外觀）只產生**一次**當作範本。
  每一格拿到 `clone()`，只複製幾個指標和數字，沒有 JSON、也沒有查詢。
- 遊戲可以加入自己的型別（`f.add("hp_bar", …)`），不必改引擎。

### 4.8 從 JSON 產生遊戲物件

遊戲物件用同樣的方式產生，只是高一層。它們的資料已經存在：

```
關卡檔    "legend": {"m": "monster:slime"}，格子 'm' 在 (4, 7)，
          "objects": {"4,7": {"look": {...}, "events": [...]}}
類型檔    enemies.json  "slime": {"hp": 35, "atk": 18, "def": 1, "look": {...}}
```

1. **載入樓層時**，對每個實體：從圖例取得種類與 id（`monster` / `slime`），從類型檔取得類型紀錄，
   從 `objects` 取得物件自己的設定。
2. **遊戲物件工廠**以種類為鍵（`monster`、`item`、`npc`、`door`、`key`、`stairs`、`event`），呼叫該類別的
   `fromJson(record, own, ctx)`：
   - `MonsterObject` 讀取 hp、atk、def 與佔格；
   - `ItemObject` 讀取效果；
   - `NpcObject` 讀取對話檔；
   - 每個都保存自己的規則資料。
3. **它的外觀**是類型外觀上面疊物件自己的外觀，**在這裡**合併一次，不是每幀。它經過算圖工廠（4.7），
   產生的算圖物件加為遊戲物件的子物件。只用類型外觀的格子則複製那個類型的範本。
4. **每一幀**走訪這棵樹：先 `update(dt)`，再 `RenderTree(ren)`。`Game` 不再對每個實體呼叫 `lookOf`、
   `typeSprite` 或 `drawObjectAnim`，`objAnims_`、`idleAtlases_` 與 `doorAtlases_` 也消失。
   待機動畫與開門動畫就是 `clip` 算圖物件。
5. **存檔**不變。遊戲狀態（已使用、已開啟、剩餘 HP）留在存檔資料（`EntityStatus`），不在物件裡。
   物件由資料加存檔狀態重建，所以讀檔會產生一樣的物件。

關卡編輯器經由同一個核心函式庫使用相同的工廠。它顯示的就是遊戲的繪圖，而不是複製品
（目前 `StageCanvas::paintGameScene` 重複了 `Game` 的外觀規則）。

### 4.9 粒子、glTF 與編輯器採用同一個概念

- **粒子：**`.prt` 檔是一個 `ParticleRes`。它的圖集是快取裡的 `AtlasRes`，和動畫用的是同一份。
  地圖上的特效是 `ParticleObject`。預覽（`playPreviewFx`）是預覽戳記裡的 `ParticleObject`。
- **glTF：**`.glb` / `.gltf` 檔是一個 `ModelRes`：解析好的模型與它的 GPU 緩衝區和貼圖，只上傳一次。
  每個 `ModelObject` 有自己的 `Pose`（或群眾實例），所以一百個騎士共用同一個模型。烘焙的動畫貼圖
  （`uploadAnimTexture`）屬於 `ModelRes`。
- **編輯器**（`stage_event_editor`、`anim_editor`、`particle_editor`、`gltf_viewer`、圖集工具）：
  - 每個程式在它的 bgfx 算圖器上有一個 `ResourceManager`；
  - `GameTextures` 變成它上面的薄薄一層；
  - QPainter 路徑（`SpriteImageCache`）以它裁切來源的 `AtlasRes` 為鍵；
  - `checkFiles()` 取代各工具自己的重新載入程式，所以在圖集工具重建的圖集會出現在每個開著的編輯器裡。

### 4.10 遊戲物件：每種一個類別，依狀態繪製（擁有者的設計，2026-10-17）

目前地圖上每個物件都是同一個 struct（`Entity`：位置、一個 `kind` 字串、外觀）。它會做什麼，由 `Game` 裡對 `kind`
的字串判斷決定：
- `game_scene_draw.cpp` 有 6 處決定怎麼畫；
- `game_input.cpp` 有 11 處決定碰到它會發生什麼；
- `game_assets.cpp` 有 4 處決定載入樓層時做什麼。

它的狀態分散在 `Entity::consumed`、`Game::entityStatus_` 與 `Game::mapFx_`。改成：

```
GameBaseObject : Node                 （在樓層的樹裡：階層面板會顯示它）
 ├─ cell、footprint、kind、id
 ├─ status            "idle"、"hurt"、"opening"、"opened"、"defeated" …
 ├─ EventList         它連結的事件 + 它在 EventBus 上監聽的事件
 ├─ RenderSystem      狀態 -> 算圖物件（4.7），從 JSON 產生一次；狀態改變時切換
 ├─ update(dt)        目前的算圖物件前進（動畫播放）；播完的動畫可以切到下一個狀態
 ├─ render(ren)       在它的格子畫目前的算圖物件：圖片只需要位置，動畫有它的檔案與片段名稱
 └─ virtual onTouch(player)
     MonsterObject（戰鬥；hurt -> defeated）、ItemObject（collected）、DoorObject（鑰匙；opening -> opened）、
     GateObject、StairsObject、NpcObject（對話）、EventObject（劇情牌）、PlayerObject
```

- **每個狀態的外觀**是類型紀錄裡的 `looks` 表。舊的 `sprite` / `look` 欄位仍然有效，被當作 `idle` 外觀讀取：
  ```json
  "slime": { "hp": 35, "atk": 18, "def": 1,
    "looks": { "idle":     { "type": "clip", "anim": "anim/monster_idle.anim", "clip": "idle_slime", "loop": true },
               "hurt":     { "type": "clip", "anim": "anim/monster_fx.anim", "clip": "hurt", "next": "idle" },
               "defeated": null } }
  ```
  放置的物件自己的外觀（關卡編輯器的 `objects`）逐個狀態覆蓋類型的外觀。沒有外觀的狀態退回 `idle`。
- **事件：**物件擁有它連結的事件（目前由 `Game::fireLinkedEvents` 讀取），並訂閱 `EventBus`（例如門訂閱「使用了
  鑰匙」，NPC 訂閱「劇情旗標已設定」）。事件改變狀態，算圖系統跟著切換外觀。
- **依 `kind` 的工廠**產生物件，就像 `RenderObjectFactory` 依 `"type"` 產生。
- **存檔不變：**狀態以和現在相同的 `entityStatusKey` 儲存，所以舊存檔能讀取。
- **`Game` 保留的：**大家共用的規則（戰鬥、背包、劇情）。由物件在 `onTouch` 裡要求這些規則，而不是由 `Game`
  一個個問物件是什麼。

**已完成（2026-10-17）**，在 [src/core/game/core/map_objects.h/.cpp](../../src/core/game/core/map_objects.h)：
- `MapObject` 與各種類（`MonsterObject`、`ItemObject`、`EventObject`、`NpcObject`、`GateObject`、`DoorObject`、
  `StairsObject`、`PlayerObject`），以及 `MapFloor`：載入樓層時產生它們，並把它們當作同一個根節點的子節點保存；
- 每個物件依狀態的外觀，在載入樓層時讀取一次（舊欄位當作 `idle`；門與柵門的 `opening`；資料的 `looks` 表）。

移進物件的：
- `Game::draw` 的物件階段（格子現在是 `Game::drawTileLayer`），連同每幀的 `Game::lookOf`、`drawObjectAnim`、
  `drawIdleAnim` 與 `mapFx_`；
- `movePlayer` 的種類判斷：柵門（`bumpBeforeStep`）、大型怪物（`bumpBig`）、踩上格子（`onTouch`）；
- `interact` 的 NPC 判斷（`onInteract`）；
- `openDoor` 的開門動畫（`DoorObject` 播放 `opening`）。

所有遊戲截圖原封不動通過。`looks` 表（`idle` / `opening` 以外的狀態）目前沒有出貨資料使用，所以還沒有自動測試。

### 4.11 樓層以批次繪製（擁有者的要求，2026-10-17）

目前一幀怎麼畫：所有 quad 放進同一個清單，算圖器（[bgfx_renderer.cpp](../../src/engine/src/bgfx_renderer.cpp)）
對每一段貼圖與混色相同的 quad 發出一次 draw call。F3 HUD 會顯示次數。所以 draw call 數就是繪製順序中貼圖或混色
改變的次數。目前在一層樓上：
- **兩張貼圖輪流使用。**遊戲圖集（214×178）與 `fx.atlas`（126×170）是兩張不同的貼圖。怪物待機動畫與開門動畫兩者都用
  （`game:slime` 搭配 `fx:glow`、`fx:spark`、`fx:star`）。物件依列繪製，所以每隻有光暈的怪物都會切換
  game → fx → game：畫面上每隻怪物約多出 2 次 draw call。
- **動畫裡的加色光暈**會切換混色，再次切斷批次。
- **每幀全部重建：**可見格子的 quad、每個物件的外觀（JSON 合併）、排序清單。CPU 重做結果根本沒變的工作。

樓層改成以最少的呼叫次數繪製：

1. **一層樓一張貼圖。**快取載入小圖集時，把它們的頁面打包進一張共用頁面。遊戲圖集和 `fx.atlas` 放進一張 512×512
   的頁面還綽綽有餘。`AtlasRes` 保留自己的區域與 id（`fx:glow` 仍然指 `fx.atlas`），只是 UV 指向共用頁面。
   算圖器的圖片貼圖就是那張頁面，UI 和現在一樣借用它。於是整層樓（格子、怪物、光暈、門、玩家）都從同一張貼圖繪製。
   大型圖集（3D 貼圖、大張美術）保留自己的頁面。
2. **加色 quad 集中。**在物件階段，動畫的加色 quad（光暈、火花）放在同一列所有一般 quad 之後畫，而不是夾在中間。
   這樣每一列最多切換一次混色，而不是每隻怪物一次。列仍然由後往前排序，所以重疊的東西順序不變。
3. **靜態格子層。**樓層的格子 quad 在載入樓層時建立一次，放進算圖器保存的緩衝區
   （`IRenderer::createStaticBatch` / `drawStaticBatch`）。只有格子改變（門打開、章節顏色改變）時才重建。
   相機用變換移動這一層，而不是重建它。34×31 的樓層約 1,000 個 quad 只上傳一次，取代每幀約 300 個可見格子。
4. **物件保留自己的 quad。**`SpriteObject` 已經會保留它的 quad，直到位置、色調或圖集改變。`AnimPlayer` 保留沒有
   移動的姿勢的 quad。樓層的樹只在物件換列（遊蕩者移動）時重新排序，不是每幀。相機外的物件跳過，和格子現在一樣。
5. **量測，而不是猜測。**一個熱鬧樓層的截圖測試也檢查 draw call 數（`lastDrawCalls`）。目標：**格子一次、
   物件一次**，再加上每段加色一次。多出一次貼圖切換的修改會讓測試失敗。

**工作順序**（第 6 節的第 5 步）：
1. 遊戲物件類別、依狀態的算圖系統與樓層樹：畫面相同，黃金截圖不變。
2. 批次：共用頁面、加色集中、靜態格子層與 draw call 測試。畫面同樣相同，只有 draw call 數改變。
3. `toms_editor` 裡的階層與細節面板，顯示真正的樹。

## 5. 這樣怎麼讓遊戲更快、更小

| 改變 | 效果 |
|---|---|
| 每個檔案一個 `AtlasRes`；頁面共用 | 每頁只上傳一次。目前被待機動畫、開門動畫與物件動畫共用的圖集會上傳三次 |
| 載入樓層時解析外觀；範本加 `clone()` | 每幀每個實體不再合併 JSON、找欄位、解析字串。這正是編輯器重新整理變慢的最大成本 |
| 算圖物件持有已解析的指標（區域、動畫） | 一次繪製只是幾次乘法加一個 `drawSprite`。目前每幀都要依名稱查圖集區域（動畫還要 `find`） |
| 每層樓、每場戰鬥、每個預覽一個戳記 | 記憶體跟著畫面上的東西走；長時間遊玩不會一直變大 |
| 載入失敗連同錯誤一起快取 | 缺少的檔案只花一行記錄，而不是每幀讀一次檔 |
| `report()` 與 `DumpLeaks` | 載入了什麼、多大、誰持有：每個平台的記憶體預算（網頁與 Android 最重要） |
| 之後在工作執行緒 `prefetch` | 換樓層時不會卡在讀檔與 PNG 解碼 |

## 6. 分步計畫

每一步都能單獨建置、遊戲畫面不變，並附上測試。在第 5 步之前，黃金截圖都不能改變。**第 1–3 步已完成**（第 9 節）；
接下來是第 4–6 步。

1. **核心資源層：**
   - `src/core/engine` 裡的 `resource.h/.cpp`、`resource_manager.h/.cpp`；
   - `TextureRes`、`AtlasRes`、`AnimRes`、`ParticleRes`、`JsonRes`；
   - 戳記與 `report()`。

   單元測試：同一路徑拿到同一個指標；缺少的檔案只 `Failed` 一次；圖集的頁面是共用的；`popStamp` 只釋放
   該戳記載入的東西，並保留被持有或 `keep()` 的；重新載入會增加版本並保留同一個物件。
2. **遊戲的載入改用它：**
   - `loadPreviewAtlases`、`idleAtlases_`、`doorAtlases_`、`objAnims_` 與預覽改用 `get<AnimRes>` 和 `get<ParticleRes>`；
   - `game.atlas` 變成一個 `AtlasRes`；
   - UI 借用它的 `TextureRes`（取代 `shared_textures.h`）。

   畫面完全不變，由黃金截圖證明。
3. **算圖物件與工廠：**
   - `render_object.h/.cpp`，包含 `sprite`、`clip`、`particle`、`group`、`text`；
   - `look` 轉接器。

   單元測試：每種型別從 JSON 產生；未知型別或缺少的圖片會得到錯誤與標記；`clone()` 共用資源；`toJson()` 來回一致。
4. **`model`：**`GltfRenderer` 之上的 `ModelRes` 與 `ModelObject`；`gltf_viewer` 經由管理器載入。
5. **從 JSON 產生遊戲物件：**
   - 遊戲物件工廠；
   - 載入樓層時建立樹，每幀畫樹；
   - 移除 `lookOf`、`drawObjectAnim` 與 `drawIdleAnim`；
   - 每層樓、每場戰鬥一個戳記。

   只有在繪製順序刻意改變的地方才更新黃金截圖。
6. **編輯器：**每個程式一個管理器，`GameTextures` 建在它上面，以 `checkFiles()` 取代各工具的重新載入，
   關卡畫布經由遊戲的算圖物件繪製。

測試選擇：`src/core/engine/resource*` 與 `render_object*` 底下的新檔案，在 `tools/test_select.py` 中對應到
引擎單元測試與遊戲的黃金截圖測試。每一步都加上自己的規則與測試。

## 7. 使用它的程式碼規則

- 繪圖時絕不直接載入檔案，要向管理器要：`get<T>(path)`。
- 對你繪製來源的東西保存 `shared_ptr`。絕不在沒有擁有者的情況下保存原生的區域、動畫或貼圖代號。
- 在載入資料時把 JSON 轉成物件。絕不在 `update` 或 `Render` 裡讀 JSON。
- 任何會載入、之後又會消失的東西（樓層、戰鬥、預覽、編輯器文件）都用戳記包起來。
- 新的可繪物是一個 `RenderObject` 子類別，有 `fromJson`、`clone` 與 `toJson`，再加一行 `add`。
  新的遊戲物件種類在遊戲層做同樣的事。

## 8. 擁有者的決定（2026-10-17）

1. **JSON 欄位名稱：**`"type"` + `"atlas"` + `"name"`。
2. **釋放時機：**遊戲還小，所以不每層樓釋放。沒人用的資源留在快取，超過記憶體預算時才從最久沒用的開始釋放（4.4）。
   戳記保留給預覽與編輯器文件。
3. **重新載入：**編輯器也要。遊戲在 `toms_editor`、除錯版，以及加上 `--hot-reload` 時會重新載入改變的檔案。
   Qt 工具（關卡編輯器、動畫編輯器 …）在第 6 步改用管理器。
4. **順序：**先做第 1、2、3 步。

擁有者的其他意見：不用 `wchar_t`（UTF-8 字串，只在作業系統需要時轉換：`vfs.h` 在 Windows 上經 UTF-16 開啟 UTF-8 路徑）；
AddRef 與 Release 必須自動且安全（4.4）；要比 `GetMe` 更好的方法（4.7）；用 JSON 取代 XML。

## 9. 完成的內容（第 1–3 步）

| 檔案 | 內容 |
|---|---|
| [src/core/engine/resource.h/.cpp](../../src/core/engine/resource.h) | `Resource`、`ResourceManager`（依路徑快取、預算、戳記、`checkFiles`、`report`）、`ResourceStamp` |
| [src/core/engine/resource_types.h/.cpp](../../src/core/engine/resource_types.h) | `TextureRes`、`AtlasRes`、`AnimRes`、`ParticleRes`、`FontRes`、`JsonRes`，以及 `AtlasList`（一個檔案的圖集組成的 `AtlasSet`） |
| [src/core/engine/render_object.h/.cpp](../../src/core/engine/render_object.h) | `RenderObject` 與 `RenderObjectFactory`；`sprite`、`clip`、`particle`、`text`、`group`、`missing`；`lookToRenderJson` |
| [src/core/engine/vfs.h](../../src/core/engine/vfs.h) | `vfsOpen` / `vfsPath` / `vfsUtf8`：在 Windows 上能開啟 UTF-8 路徑（中文資料夾名稱可用） |
| `Game`（game.h、game_assets.cpp、game_scene_draw.cpp、game.cpp、game_input.cpp） | 一個 `ResourceManager`。待機動畫、開門動畫、物件動畫與兩種預覽都從它取得；`loadPreviewAtlases` 以及各使用者自己的圖集與貼圖清單都移除了。載入的圖片圖集以 `@sprite-atlas` 提供，所有 `.../atlas/game.atlas` 都對應到它，所以動畫和以前一樣跟著美術風格 |
| `GameSession`、`main_sdl.cpp`、`toms_editor` | `SessionOptions::hotReload`、`--hot-reload`；`toms_editor` 與除錯版預設開啟 |

玩家看到的變化：畫面上沒有（黃金截圖原封不動通過）。底層的變化：
- `fx.atlas` 只載入一次，不再是待機動畫一次、開門動畫又一次；
- 缺少或損壞的動畫檔只回報一次；
- 開啟熱重載時，遊戲執行中存檔的美術、動畫與特效會在一秒內出現。

第 2 步刻意沒做的：
- UI 仍經由 `shared_textures.h` 借用圖片貼圖。遊戲自己的圖集是算圖器內建的圖片貼圖，快取只是描述它（`provide`），
  所以目前沒有好處；
- 遊戲自己的 `game.atlas` 不會熱重載（它經由 `loadSpriteAtlas` 載入；換美術風格時會重新載入）。

測試（在 `toms_tests` 裡，`tools/test_select.py` 對 `src/core/engine/resource*` 與 `render_object*` 會選到）：
- **`unit.resource_test`：**
  - 每個檔案一個物件；
  - 待機與開門動畫共用同一個 `fx.atlas`，每頁只上傳一次；
  - 失敗會被快取；
  - `provide` 與路徑對應；
  - 預算，包括預算 0 會釋放所有沒人用的；
  - 戳記與 `keep`；
  - 原地重新載入，包括失敗時保留舊內容，以及頁面改變會讓圖集的內容版本增加；
  - `detachRenderer`；
  - 含中文字的 UTF-8 路徑。
- **`unit.render_object_test`：**
  - 每種內建型別從 JSON 產生；
  - 錯誤（未知型別、缺少圖片或檔案、子物件的錯誤會說明位置）；
  - 紅色的「缺少」框；
  - 複製品與範本共用資源；
  - `toJson` 來回一致；
  - 隱藏的群組會隱藏子物件；
  - 關卡編輯器 `look` 的轉接器。
