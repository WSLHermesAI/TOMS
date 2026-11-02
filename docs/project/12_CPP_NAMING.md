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
| `psz` | a C string: `const char*`, `char*` |
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

- 2026-11-02: the rules are written (variables, then functions). The existing code is converted when the owner asks for it.
- Until then, a file keeps the style it has, so that no file is half one style and half the other. **New files** follow
  this rule.
