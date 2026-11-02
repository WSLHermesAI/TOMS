# 12 — C++ naming: scope and type in every variable's name, `f_` on every function

中文：[zh_TW/12_CPP_NAMING.md](zh_TW/12_CPP_NAMING.md).

The owner (2026-11-02): every variable's name says where it lives and what type it is.

```
<scope>_<type><Name>
```

- **scope:** `m_` a member, `l_` a local, `g_` a global (§1);
- **type:** a short lowercase prefix for its type (§2);
- **Name:** the name itself, starting with a capital letter (UpperCamelCase).

```cpp
struct Look {
    std::string m_strBase;                  // member, string
    std::map<std::string, LookState> m_mapStates;
    const LookState* f_Find(const std::string& l_strState) const;   // a function: f_
};
int g_iFrameNo = 0;                         // global, int
void Game::f_PopDamage(int l_iDmg, int l_iWho) {
    const bool l_bBig = l_iDmg > 100;       // local, bool
    auto l_fnShow = [this, l_iDmg](float l_fDelay) { /* ... */ };
}
```

Functions (§3) start with `f_`, then the name with a capital letter: `f_PopDamage`, `f_LoadBattleRig`.

## 1. Scope

| Prefix | Where the variable lives |
|---|---|
| `m_` | a data member of a class or a struct, public or private, static or not, including plain data structs |
| `l_` | inside a function or a lambda: function parameters, local variables, lambda parameters, loop variables, structured bindings, static locals |
| `g_` | outside any function or class: namespace scope, file scope (an anonymous namespace or `static`), `extern` |

Constants take the prefix of where they live: a `static constexpr` member is `m_` (`kBarCooldownMs` becomes
`m_iBarCooldownMs`); a constant at namespace scope is `g_`.

## 2. Type

| Prefix | Type |
|---|---|
| `b` | `bool` |
| `i` | `int`, `short`, `long` |
| `i8` `i16` `i64` | `int8_t`, `int16_t`, `int64_t` (and `long long`) |
| `u` | `unsigned`, `uint32_t` |
| `u8` `u16` `u64` | `uint8_t`, `uint16_t`, `uint64_t` |
| `n` | `size_t` (sizes, counts, indexes into containers) |
| `f` | `float` |
| `d` | `double` |
| `c` | `char` |
| `str` | `std::string` |
| `sv` | `std::string_view` |
| `psz` | a C string: `const char*`, `char*`, a `char` array holding text (`const char g_pszName[] = "..."`) |
| `v` | `std::vector` |
| `arr` | `std::array`, a C array |
| `map` | `std::map`, `std::unordered_map` |
| `set` | `std::set`, `std::unordered_set` |
| `list` | `std::list` |
| `dq` | `std::deque` |
| `pair` | `std::pair` |
| `tup` | `std::tuple` |
| `opt` | `std::optional` |
| `p` | a raw pointer (any type it points to) |
| `sp` | `std::shared_ptr` |
| `up` | `std::unique_ptr` |
| `wp` | `std::weak_ptr` |
| `fn` | `std::function`, a lambda kept in a variable, a function pointer |
| `it` | an iterator |
| `e` | an enum value or an `enum class` value |
| `j` | `nlohmann::json` |
| `v2` `v3` `v4` | `glm::vec2`, `glm::vec3`, `glm::vec4` (and the `ivec` / `uvec` ones) |
| `mat3` `mat4` | `glm::mat3`, `glm::mat4` |
| `q` | `glm::quat` |
| `o` | an object of any other class or struct (`Clip`, `Player`, `Quad`, ...) |

- A **reference** takes the prefix of the type it refers to (`const std::string& l_strName`).
- **`auto`** takes the prefix of the type it stands for.
- A type not in the table: choose a short lowercase prefix, add it to this table, then use it.

## 3. Functions: `f_`

The owner (2026-11-02, a second rule): every function's name starts with `f_`, then the name with a capital letter.

| Kind | Example |
|---|---|
| a free function | `toms::anim::f_Placement(...)` |
| a member function, static or not, virtual or not | `Game::f_LoadBattleRig()`, `FighterView::f_Play(...)` |
| a function in an anonymous namespace or `static` | `f_DropCrumbs(...)` |

No type prefix: `f_` already says it is a function (a variable that *holds* a function is `l_fn...` / `m_fn...`).

These keep their names, because the language or a library calls them by name:
- constructors, destructors, operators (`operator==`, conversion operators);
- `main` and the platform's entry points (`SDL_AppInit`, `android_main`, JNI `Java_...` functions);
- overrides of a third-party class's virtual functions (`bgfx::CallbackI::screenShot`, RmlUi's interfaces): the base
  decides the name;
- functions found by name: `to_json` / `from_json` (nlohmann::json), `begin` / `end` / `size` (range-for and the
  standard library), `swap`, `hash` specializations.

## 4. What the rules do not change

- **Third-party code is never touched:**
  - `src/third_party/` (json, miniaudio, stb, tweeny);
  - the libraries CMake downloads (`cmake/TomsDependencies.cmake`: bgfx, SDL3, glm, imgui, RmlUi, cgltf, freetype,
    JoltPhysics);
  - the `AIGamestyle` submodule.
- **Names that are neither variables nor functions:** classes, structs, enums and their values, namespaces, macros and
  template parameters keep their current style.
- **Names fixed from outside:** the keys in JSON files and the names the data uses stay as they are (only the C++
  variable that holds them is renamed). Members of a third-party type (for example `bgfx::Init`) keep their names.
- **Other languages:** Python scripts and shaders keep their own style. Code a tool generates (for example
  `res_keys.h` from `tools/make_res_keys.py`) follows the rule, and the generator is changed to write it.

## 5. Status

- 2026-11-02: the rules are written (variables, then functions).
- 2026-11-02: **all of the project's C++ is converted** (the owner: "make this project's c++ files to match my rule"):
  417 files, about 191,000 renamed names (42,400 declarations), by the tool of §6. Third-party code is untouched.
  The Windows build (game, engine, editors, tools, tests) builds with no errors; 20 game scenarios and the anim
  editor's headless check run as before. The web and Android code (inside `#ifdef __EMSCRIPTEN__` / `__ANDROID__`)
  was renamed from their own parses but not built (web and Android are built only when the owner asks).
- **Kept names** (367 declarations): overrides of third-party virtual functions (Qt's `paintEvent`, RmlUi's
  `LoadTexture`, Jolt's `OnContactAdded`, bgfx's callbacks), names found by lookup (`begin`, `end`, `size`, `swap`,
  `to_json`, operators, `main`), `extern "C"` functions JavaScript calls (`jsFlushSave`, ...), and what macros declare
  (the unit tests' `TOMS_TEST` functions, Qt's moc code). The renamer's report lists them all
  (`Build/naming/report.txt`).
- **Not renamed:** the comments and the documents that name old identifiers (`loadBattleRig` in a comment means
  `f_LoadBattleRig` now).

## 6. The tool: tools/naming/rename_cpp.py

```
pip install libclang                                   (once; the Python bindings)
python tools/naming/rename_cpp.py plan                 Build/naming/plan.json + report.txt (about 1 minute)
python tools/naming/rename_cpp.py apply                applies the plan
```

- It reads every C++ file with libclang (Visual Studio's, clang 22; the NDK's clang 18 for Android), each with its
  own compile flags from `Build/windows-release`, `Build/web-release-windows`, `Build/android-debug` and
  `Build/atlas-web` (`ninja -C Build/atlas-web -t compdb`).
- Each declaration and every reference to it is found by its clang USR (an instantiation of a template by the
  template's place in the source); the new name comes from its scope and its real type. A name inside a template that
  clang cannot resolve is matched by its name, and only when that is safe.
- **As a checker:** on code that follows the rules, `plan` finds next to nothing to rename (after the conversion: 9,
  all false, in a web-only file whose parse misses one header). Run it after writing a lot of new code.
- **Debug-only code:** `assert(...)` bodies and `#ifdef _DEBUG` / `#ifndef NDEBUG` blocks exist only with the Debug
  flags. Run it with `TOMS_NAMING_WIN_DB=windows-debug` for such files (`--only <file>`); after the conversion
  `leak_check.cpp` (Debug only) was renamed that way, and the Debug build checked (`tools\build.cmd windows-debug`).
- An STL allocator's `allocate` / `deallocate` (members) keep their names: the containers call them by name.
- What it cannot do, and was done by hand (`Build/naming/postfix.py`, local): a name inside a macro's body used by many
  classes (`TOMS_OBJECT`'s `f_Type`, `f_StaticType`), and a few members reached through generic lambdas.
- **Two names that differ only in case** become one (`setName` and `SetName` both `f_SetName`). `Node`'s
  `name()`/`setName()`, which only called `Object`'s `Name()`/`SetName()`, would have called themselves; they were
  removed. The report's collisions section checks one scope; check a class and its base by hand.
