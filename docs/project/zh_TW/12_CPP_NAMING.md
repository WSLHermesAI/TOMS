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
| `psz` | C 字串：`const char*`、`char*` |
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

- 2026-11-02：規則寫成（先是變數，再是函式）。現有程式碼在擁有者要求時再轉換。
- 在那之前，每個檔案維持它現有的風格，避免一個檔案一半新一半舊。**新檔案**遵守這條規則。
