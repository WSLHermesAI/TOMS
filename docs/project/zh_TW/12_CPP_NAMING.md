> 英文原文：[12_CPP_NAMING.md](../12_CPP_NAMING.md)

# 12 — C++ 命名：每個變數的名稱都標明範圍與型別，每個函式以 `f_` 開頭

擁有者（2026-11-02）：每個變數的名稱都說明它在哪裡、是什麼型別。

```
<範圍>_<型別><名稱>
```

- **範圍：** `m_` 成員、`l_` 區域、`g_` 全域（§1）；
- **型別：** 代表型別的小寫短前綴（§2）；
- **名稱：** 名稱本身，以大寫字母開頭（UpperCamelCase）。

```cpp
struct Look {
    std::string m_strBase;                  // 成員，string
    std::map<std::string, LookState> m_mapStates;
    const LookState* f_Find(const std::string& l_strState) const;   // 函式：f_
};
int g_iFrameNo = 0;                         // 全域，int
void Game::f_PopDamage(int l_iDmg, int l_iWho) {
    const bool l_bBig = l_iDmg > 100;       // 區域，bool
    auto l_fnShow = [this, l_iDmg](float l_fDelay) { /* ... */ };
}
```

函式（§3）以 `f_` 開頭，接著是大寫開頭的名稱：`f_PopDamage`、`f_LoadBattleRig`。

## 1. 範圍

| 前綴 | 變數在哪裡 |
|---|---|
| `m_` | class 或 struct 的資料成員，不論 public 或 private、static 與否，包含純資料的 struct |
| `l_` | 函式或 lambda 之內：函式參數、區域變數、lambda 參數、迴圈變數、結構化綁定、static 區域變數 |
| `g_` | 任何函式與 class 之外：命名空間範圍、檔案範圍（匿名命名空間或 `static`）、`extern` |

常數依所在位置取前綴：`static constexpr` 成員是 `m_`（`kBarCooldownMs` 改為 `m_iBarCooldownMs`）；命名空間範圍的常數是 `g_`。

## 2. 型別

| 前綴 | 型別 |
|---|---|
| `b` | `bool` |
| `i` | `int`、`short`、`long` |
| `i8` `i16` `i64` | `int8_t`、`int16_t`、`int64_t`（以及 `long long`） |
| `u` | `unsigned`、`uint32_t` |
| `u8` `u16` `u64` | `uint8_t`、`uint16_t`、`uint64_t` |
| `n` | `size_t`（大小、數量、容器的索引） |
| `f` | `float` |
| `d` | `double` |
| `c` | `char` |
| `str` | `std::string` |
| `sv` | `std::string_view` |
| `psz` | C 字串：`const char*`、`char*`、存放文字的 `char` 陣列（`const char g_pszName[] = "..."`） |
| `v` | `std::vector` |
| `arr` | `std::array`、C 陣列 |
| `map` | `std::map`、`std::unordered_map` |
| `set` | `std::set`、`std::unordered_set` |
| `list` | `std::list` |
| `dq` | `std::deque` |
| `pair` | `std::pair` |
| `tup` | `std::tuple` |
| `opt` | `std::optional` |
| `p` | 原始指標（不論指向什麼型別） |
| `sp` | `std::shared_ptr` |
| `up` | `std::unique_ptr` |
| `wp` | `std::weak_ptr` |
| `fn` | `std::function`、存在變數中的 lambda、函式指標 |
| `it` | 迭代器 |
| `e` | 列舉值或 `enum class` 的值 |
| `j` | `nlohmann::json` |
| `v2` `v3` `v4` | `glm::vec2`、`glm::vec3`、`glm::vec4`（以及 `ivec` / `uvec`） |
| `mat3` `mat4` | `glm::mat3`、`glm::mat4` |
| `q` | `glm::quat` |
| `o` | 其他任何 class 或 struct 的物件（`Clip`、`Player`、`Quad`……） |

- **參考**取它所參考型別的前綴（`const std::string& l_strName`）。
- **`auto`** 取它所代表型別的前綴。
- 表中沒有的型別：選一個小寫短前綴，先加入這張表，再使用。

## 3. 函式：`f_`

擁有者（2026-11-02，第二條規則）：每個函式的名稱以 `f_` 開頭，接著是大寫開頭的名稱。

| 種類 | 範例 |
|---|---|
| 自由函式 | `toms::anim::f_Placement(...)` |
| 成員函式，不論 static 或 virtual 與否 | `Game::f_LoadBattleRig()`、`FighterView::f_Play(...)` |
| 匿名命名空間或 `static` 的函式 | `f_DropCrumbs(...)` |

不加型別前綴：`f_` 已表示它是函式（**存放**函式的變數是 `l_fn...` / `m_fn...`）。

以下保留原名，因為語言或函式庫依名稱呼叫它們：
- 建構式、解構式、運算子（`operator==`、轉型運算子）；
- `main` 與平台的進入點（`SDL_AppInit`、`android_main`、JNI 的 `Java_...` 函式）；
- 覆寫第三方類別的 virtual 函式（`bgfx::CallbackI::screenShot`、RmlUi 的介面）：名稱由基底類別決定；
- 依名稱尋找的函式：`to_json` / `from_json`（nlohmann::json）、`begin` / `end` / `size`（range-for 與標準函式庫）、`swap`、
  `hash` 特化。

## 4. 這些規則不改變的東西

- **絕不更動第三方程式碼：**
  - `src/third_party/`（json、miniaudio、stb、tweeny）；
  - CMake 下載的函式庫（`cmake/TomsDependencies.cmake`：bgfx、SDL3、glm、imgui、RmlUi、cgltf、freetype、JoltPhysics）；
  - `AIGamestyle` 子模組。
- **既不是變數也不是函式的名稱：** class、struct、列舉與其值、命名空間、巨集與樣板參數維持目前的風格。
- **由外部決定的名稱：** JSON 檔中的鍵與資料使用的名稱維持不變（只改存放它們的 C++ 變數名）。第三方型別的成員（例如
  `bgfx::Init`）保留原名。
- **其他語言：** Python 腳本與 shader 維持各自的風格。工具產生的程式碼（例如 `tools/make_res_keys.py` 產生的 `res_keys.h`）
  遵守這條規則，並修改產生器讓它這樣寫出。

## 5. 狀態

- 2026-11-02：規則寫成（先是變數，再是函式）。
- 2026-11-02：**專案所有的 C++ 已轉換**（擁有者：「make this project's c++ files to match my rule」）：417 個檔案、約 19.1 萬
  個名稱（4.24 萬個宣告），用 §6 的工具完成。第三方程式碼未更動。Windows 建置（遊戲、引擎、編輯器、工具、測試）沒有錯誤；
  20 個遊戲情境與動畫編輯器的無介面檢查照常執行。Web 與 Android 的程式碼（`#ifdef __EMSCRIPTEN__` / `__ANDROID__` 之內）
  依各自的解析改名，但沒有建置（Web 與 Android 只在擁有者要求時建置）。
- **保留的名稱**（367 個宣告）：覆寫第三方 virtual 函式的（Qt 的 `paintEvent`、RmlUi 的 `LoadTexture`、Jolt 的
  `OnContactAdded`、bgfx 的回呼）、依名稱尋找的（`begin`、`end`、`size`、`swap`、`to_json`、運算子、`main`）、JavaScript
  呼叫的 `extern "C"` 函式（`jsFlushSave`……），以及巨集宣告的（單元測試的 `TOMS_TEST` 函式、Qt 的 moc 程式碼）。改名工具的
  報告列出全部（`Build/naming/report.txt`）。
- **沒有改的：** 提到舊名稱的註解與文件（註解中的 `loadBattleRig` 現在是 `f_LoadBattleRig`）。

## 6. 工具：tools/naming/rename_cpp.py

```
pip install libclang                                   （一次；Python 綁定）
python tools/naming/rename_cpp.py plan                 Build/naming/plan.json + report.txt（約 1 分鐘）
python tools/naming/rename_cpp.py apply                套用計畫
```

- 它用 libclang（Visual Studio 的 clang 22；Android 用 NDK 的 clang 18）讀每個 C++ 檔，各用自己的編譯參數，來自
  `Build/windows-release`、`Build/web-release-windows`、`Build/android-debug` 與 `Build/atlas-web`
  （`ninja -C Build/atlas-web -t compdb`）。
- 每個宣告與它的所有參照以 clang USR 找出（樣板的具現化以樣板在原始碼中的位置對應）；新名稱來自它的範圍與實際型別。樣板內
  clang 無法解析的名稱依名稱對應，只在安全時才改。
- **當作檢查器：** 在遵守規則的程式碼上，`plan` 幾乎找不到要改的（轉換後：9 個，都是誤報，在一個 Web 專用檔，它的解析缺一個
  標頭檔）。寫了大量新程式碼後執行一次。
- **只在 Debug 的程式碼：** `assert(...)` 的內容與 `#ifdef _DEBUG` / `#ifndef NDEBUG` 區塊只在 Debug 參數下存在。這類檔案用
  `TOMS_NAMING_WIN_DB=windows-debug` 執行（`--only <檔案>`）；轉換後 `leak_check.cpp`（只在 Debug）就是這樣改名，並以 Debug
  建置檢查（`toolsuild.cmd windows-debug`）。
- STL allocator 的 `allocate` / `deallocate`（成員）保留原名：容器依名稱呼叫它們。
- 它做不到、由手動完成的（`Build/naming/postfix.py`，本機）：被許多類別使用的巨集內容中的名稱（`TOMS_OBJECT` 的 `f_Type`、
  `f_StaticType`），以及少數經由泛型 lambda 存取的成員。
- **只差大小寫的兩個名稱**會變成同一個（`setName` 與 `SetName` 都是 `f_SetName`）。`Node` 的 `name()`/`setName()` 原本只呼叫
  `Object` 的 `Name()`/`SetName()`，改名後會呼叫自己；已刪除。報告的衝突段落只檢查同一個範圍；類別與其基底類別要手動檢查。
