# Working Log — Player Item System (9-Grid Inventory UI)

Track what was done, by date.

- **2026-08-14** — **Node scene-graph system** added (`src/node.h` + `src/node.cpp`): a
  2D port of FM79979 `Frame` using glm — each object has a **parent / first-child /
  next-sibling** hierarchy and a cached **local + world transform** (`world =
  parent.world * local`, lazy dirty-cache). Inventory UI now builds a `uiRoot →
  panel → slot[]` node tree and draws slots from their world rects. Headless
  `node_test` verifies the math (8 checks pass). (commit `a174800`+)
- **2026-08-14** — Inventory overlay changed to a full-screen black splash at
  transparent 50% (alpha 0.5) so the player focuses on the item UI. (commit `78dd2b6`+)
- **2026-08-14** — Initial item system: 9-grid extendable inventory UI, usable
  items (gems/potions/EXP crystal/scroll), pickup + use + discard. (commit `53778ff`)

---

## Entry: 2026-08-14 — Shared focus splash for combat & dialogue

**Branch:** `main`  ·  **Feature:** Focus splash (common modal helper)

Battle and talking scenes now use the **same** full-screen black splash at
alpha 0.5 as the inventory, via a new common helper `Game::drawFocusSplash()`
(called by combat, dialogue, and inventory overlays). This keeps the game
focused on what is happening.

Crucially, a modal also **blocks input to background objects**: `Game::modalActive()`
returns true while combat / dialogue / inventory is open, and `movePlayer()` /
`interact()` early-return on it; the Emscripten key handler swallows keys while
any modal is active, so mouse/touch/keyboard events never reach the world behind
the splash.

![Combat focus splash](screenshots/focus/combat_splash.png)
![Dialogue focus splash](screenshots/focus/dialogue_splash.png)

## Entry: 2026-08-14 — Inventory full-screen focus splash

**Branch:** `main`  ·  **Feature:** Player item system (9-grid inventory UI)

When the inventory UI is open, the scene is now covered by a **full-screen black
splash at transparency 50%** (alpha 0.5) so the player's focus is on the open
item panel. `Game::drawInventory()` draws a `0,0,W,H` black quad with
`tint[3]=0.5f` before the grid/panel.

## What was built

1. **Inventory model** — `Player::inv` (a `std::vector<std::string>` of item ids).
   Usable items (gems, potions, EXP crystal, scroll) are picked up into the
   inventory when the player walks onto an `item:` tile. Keys/coins still apply
   immediately.
2. **9-grid, extendable UI** — drawn as a centered 3×3 panel (always ≥ 9 slots,
   grows extra rows past 9 items). Each slot shows a dark box, the item's icon
   sprite, and a yellow selection highlight. A description panel below shows the
   selected item's name + description + usage hint.
3. **Item effects** (`data/items.json` → `Game::applyItem`):
   - `potion_red` / `potion_blue` — restore 40 / 80 HP
   - `gem_atk` / `gem_def` — permanent +ATK / +DEF
   - `exp_up` — grant EXP and run the level-up chain (HP/ATK/DEF grow per level)
   - `scroll` — warp to a target floor (`warp_to: stage03`)
   - `coin` / `key_*` — gold / keys (apply immediately)
4. **Controls** (wired in browser + headless demo):
   `I` toggle inventory · arrow keys move selection · `Enter` use · `D` discard.
5. **Assets** — generated 2 item icons (`exp_up.png`, `scroll.png`) and the
   missing `npc_handmaiden.png` (referenced by every stage but previously absent).

---

## Verification (real, offscreen PNG readback on the Linux/Vulkan path)

The engine was run headless (lavapipe) and screenshots captured at each step.

### 1. Inventory open (9-grid with icons + selection + description)
![Inventory open](screenshots/inventory/inventory_open.png)

The scene is covered by a **full-screen black splash at transparent 50%** (alpha
0.5) so the player focuses on the item UI. The 3×3 grid renders on top with
distinct item icons, a yellow highlight on slot 0 (red potion), and the
description "紅血瓶 立即恢復 40 點生命。" below.

### 2. After using the EXP crystal (level-up)
![After use EXP](screenshots/inventory/after_use_exp.png)

Using the EXP crystal raised the player from **LV 3 → LV 4**, with HP 120→140,
ATK 12→16, DEF 4→6. The used item was consumed from the grid.

### 3. After using a potion (heal)
![After use potion](screenshots/inventory/after_use_potion.png)

Using the red potion healed the player; the item was removed from the grid.

---

## Builds verified

| Target | Command | Result |
|--------|---------|--------|
| Linux / Vulkan | `cmake --build build-linux` | ✅ green, demo ran, 14 frames |
| WebGL2 (browser) | `emcmake cmake -B build-web -DWEB=ON` | ✅ green |
| WebGPU (browser) | `emcmake cmake -B build-webgpu -DWEB=ON -DWEB_BACKEND=WebGPU` | ✅ green |

Both standalone browser builds were regenerated into `web-gl/` and `web-gpu/`.

---

## How to run

Desktop (Vulkan):
```bash
cmake -S . -B build && cmake --build build -j4 && ./build/tower_vulkan
```
Browser:
```bash
./build_web.sh            # builds web-gl/ (WebGL2) and web-gpu/ (WebGPU)
python3 -m http.server 8099
# open http://localhost:8099/web-launch.html, press I for inventory
```

**Note:** The screenshots above are from the headless Vulkan (lavapipe) render
path. The same code runs in the browser backends; open the WebGL2/WebGPU build
and press `I` to see the live inventory UI.
