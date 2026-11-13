# 21 — The battle in 3D: a 3D stage, 2D fighters

> Status (2026-11-12): **B1 and B2 built** (the stage, the 2D fighters on it, the camera moves), and the fighters'
> sprites are **drawn in the 3D scene** (§4.3: lit by its lights, depth tested); B3–B5 next. The owner answered Q88–Q93 with the
> proposals ("do it", 2026-11-11): §7.

## 1. What the owner asked

> "Now I would like to make battle scene use a 3D camera, the scene is 3D model with a skybox and keep monster and
> player character as 2D, but the particles and some special move should use 3D way to make it."

Today's battle (the screenshot of 2026-11-11): a painted moonlit ruin fills the screen, the archer stands front left,
the moss frog in the middle, and the HUD sits on top (the instruction bar, the HP bars, the Ready / Defend buttons,
the super-move dots).

The battle after this work:

| Part | Today | In 3D |
|---|---|---|
| The place | one painted picture (an `.anim` backdrop, battle.json `"backdrops"`) | a 3D model (ground, ruins, trees) and a sky around it |
| The camera | none: the screen is the picture | a perspective camera that breathes, shakes on hits and moves for special moves |
| The hero and the monsters | 2D clips drawn in a screen box | **still 2D**, the same clips, standing on the 3D ground: nearer is bigger, and they get a shadow |
| Particles | 2D, in screen pixels | **3D**: sparks, dust and magic in the scene, behind and in front of the fighters, hidden by ruins |
| Special moves | 2D effects | 3D: arrows flying into depth, magic circles on the ground, pillars of light, sword arcs, shockwave rings |
| The HUD, hints, numbers | RmlUi on top | unchanged |

## 2. What the engine already has

| Part | Where | Used for |
|---|---|---|
| glTF models: PBR, skinning, animation, cameras | `GltfRenderer` (src/engine/src/gltf_renderer.*), [08_GLTF](../engine/08_GLTF.md) | the arena model |
| An HDR sky behind the models, and the light it gives them | `gltf_environment.*`, `Frame::background` | the skybox and its lighting |
| A 3D scene behind the sprites and the UI | `TitleScene` + `BgfxRenderer::setSceneViews` (the title's city and sea) | the same slot for the battle |
| A procedural sky (Preetham) and the ocean | `OceanRenderer`, [21_OCEAN_VIEWER](../engine/21_OCEAN_VIEWER.md) | a sea battle later, a sky without an HDR file |
| Fighters that draw themselves into a box | `FighterView` (fighter_view.h) | unchanged: the box comes from the 3D position |
| Particles: curves, colours, bursts, GPU or CPU | `particle_fx.*` (2D: `vec2` positions in pixels) | extended with 3D emitters (§4.4) |

## 3. The idea in one picture

```
 sky (HDR, all around)
   ruins, trees (glTF)              <- 3D, depth-tested
        monster mark  o  <- the frog's feet; projected to the screen -> its 2D box
   ground (glTF)
            hero mark  o            <- the archer's feet -> his 2D box (bigger: nearer)
 camera: behind and above the hero's shoulder, looking at the monster
 HUD (RmlUi) on top, as today
```

## 4. Design

### 4.1 The battle stage (data)

battle.json gets `"stages3d"`, chosen by floor the same way `"backdrops"` are:

```json
"stages3d": [
  { "floors": [1, 10], "model": "models/arena_moon_ruins.glb", "sky": "skies/moonlit_night.hdr",
    "camera": { "eye": [-1.6, 1.5, 4.2], "look": [0.4, 0.9, 0.0], "fov": 45 },
    "marks": { "hero": [-1.2, 0, 2.0], "monster": [0.6, 0, -1.0], "monsters3": [[-0.6,0,-1.2],[0.6,0,-1.0],[1.8,0,-1.3]] },
    "light": { "sky": 1.0, "sun": [0.3, -1.0, -0.4], "sunColor": [0.6, 0.7, 1.0] } }
]
```

The files go in resources.json like every other file (a `pattern` for the family). A floor without a 3D stage, a
backend without mesh shaders, or the "2D battle" setting keeps today's painted backdrop.

### 4.2 The camera

`BattleCamera`: one base shot per stage (above), and moves on top of it, each one data:

| Move | When | What |
|---|---|---|
| breathe | always | a slow sway of a few centimetres, so the picture lives |
| shake | a hit, a stomp | a short 3D shake (strength by damage), replaces today's screen shake |
| push in | charging the bow, the staff's last spell | a slow dolly towards the target |
| orbit | a special move, a boss's entrance | part of a circle around the target, then back |
| slow motion | the last blow | time slowed for the camera and the effects |

special_moves.json and the monsters' attacks name a move (`"camera": "orbit"`), the same reusable-blocks way the
effects are named today ([16](16_SKILLS_AND_PLAYS.md)).

### 4.3 2D fighters standing in 3D

Each fighter has a mark on the ground (its feet). The fighter is an object **of the 3D scene** (the owner,
2026-11-12: "it should treat 2D object as a 3D object ... dynamic light should also affect 2D objects"):

Its 2D sprites are drawn **directly in the 3D scene**, with no picture in between (the owner, 2026-11-12: "it should
all render like a 2D things before but adjust scale to fix 3D looking"):

1. The game draws the fighter as on the 2D screen (`FighterView::f_Draw` into its layout box: the clips, the weapons,
   the 2D/3D fighter switch of [18](18_FIGHTER_VIEWS.md), all untouched), but inside a **world group**
   (`IRenderer::f_BeginWorld` / `f_EndWorld`). The group's matrix (`Game::f_Battle3DSpriteMatrix`) turns each design
   pixel into a point in metres: the box's bottom middle on the fighter's feet, the box's height the fighter's height,
   the plane upright and parallel to the screen.
2. The 2D renderer (`BgfxRenderer`) keeps the group's triangles in metres (`f_WorldGroups`): every quad and mesh, in
   the order drawn, in runs of one texture and blending. Nothing is drawn on the screen and nothing is cut: a raised
   arm, the bow over the head, a lunge outside the box are placed the same way.
3. After the game has drawn, the host gives the groups to the stage (`BattleStage::f_DrawSprites`), which draws each run
   with the models' own shader (`GltfRenderer::f_DrawSprites`): **lit** by the moon, the brazier fires and the hit
   flashes (PBR, the plane's normal toward the camera), **depth tested** (a ruin in front hides the fighter), alpha
   blended and sorted with the scene's other blended draws by distance (smooth edges; the nearer fighter over the
   farther one). A little of its own light (`"glow"`, ÷ exposure) keeps it readable in the dark; an additive part (a
   glow) adds its light, unlit.
4. The stage's fires are world groups too, not lit: their particles at the brazier (100 px a metre), behind a fighter
   standing in front of them.

Inside a world group a layer's effect (glow, motion blur, outline: [18 node effects](../engine/18_NODE_EFFECTS.md)) is
drawn straight through without its effect, and GPU-simulated particles are not drawn (a screen-space feature each; B3).
A soft round shadow is drawn on the ground under each fighter. The fighters' screen boxes are still projected from the
marks, so the hints, the damage numbers and today's 2D hit effects stay at their places. When the renderer cannot
place sprites (`f_SupportsWorld` false) or the row says `"sprites": {"off": true}`, the fighters are drawn 2D on the
screen over the stage, as in B1.

### 4.4 3D particles

The `.particle` format gets `"space": "3d"` emitters. The same curves, colours, ranges, bursts and blending, with
positions in metres (`vec3`), gravity in m/s², and 3D shapes:

| Shape | For |
|---|---|
| point / sphere / cone | sparks, impacts, dust, debris |
| ring on the ground | shockwaves, landing dust |
| trail (ribbon) | an arrow's streak, a sword's arc, a fireball's tail |
| ground decal | magic circles, scorch marks, frost |
| flash light | a short point light that lights the arena (a hit, a spell) |

They are drawn camera-facing in the 3D pass, depth-tested against the arena, after it and before the HUD. Effects
start from a fighter's mark, a bone of its clip (projected back into the world) or a stage point. The battle editor
([13](13_BATTLE_EDITOR.md)) previews them later (B5).

### 4.5 Special moves in 3D

| Move | 3D version |
|---|---|
| Bow: the charged shot | the arrow flies into depth with a trail, the camera pushes in on the release, a burst and a ring of dust at the monster |
| Bow: the gathering and explosion ([20](20_BATTLE_EFFECTS.md) §4) | lights gather in a sphere around the monster, then a shockwave ring on the ground |
| Staff: the spells | a magic circle on the ground under the target, a pillar of light, orbs circling at different depths |
| Sword: the slash | a 3D arc ribbon across the monster, sparks thrown into the scene |
| Monster attacks ([20](20_BATTLE_EFFECTS.md) §5) | a big monster's projectile comes **at the camera**, the shake by its power, the dust under its stomp |

### 4.6 What stays 2D

The HUD and every RmlUi screen, the hints over the fighters (the staff's circles, the "!" before a hit, the target
line), the damage numbers, and the tutorial. They are screen information and stay sharp and readable. They follow
the fighters' projected positions.

### 4.7 Platforms and speed

Windows first ([CLAUDE.md](../../CLAUDE.md)). The arena is a small scene: one model, one sky, a few lights. Particles
are budgeted like today's. Web and Android keep the 2D backdrop until the owner asks for them (Q92). The quality tiers
of the ocean ([21_OCEAN_VIEWER](../engine/21_OCEAN_VIEWER.md)) are the model for them later.

## 5. Phases

| Phase | What | Checked by |
|---|---|---|
| **B1** ✅ built | the stage: arena model + sky + base camera; 2D fighters on their marks with shadows; the HUD as today; fallback to the painted backdrop | a battle scenario's reference picture in 3D, and the old one with 3D off |
| **B2** ✅ built | the camera moves: breathe, 3D shake, push in, orbit, slow motion; named by moves and attacks | scenario pictures at fixed times |
| **B3** | 3D particles (§4.4) and today's battle effects moved to 3D: hits, sparks, projectiles | particle unit tests, scenario pictures |
| **B4** | the special moves of §4.5 | scenario pictures per weapon |
| **B5** | the battle editor: the 3D stage, the camera moves and the 3D effects in its preview | the editor's self-test |

## 6. Art

The reference picture is a painted, moonlit, slightly realistic ruin. For a 3D stage of the same mood:

| Source | Fits | Cost |
|---|---|---|
| Poly Haven (CC0): night HDRI skies, stone ground textures, scanned ruin and rock models | close to the picture's mood | free; the files are large, so they are cut down for the game |
| Low-poly packs (Quaternius, Kenney; CC0) | a toy look, not the painted one | free, small |
| A simple stage made here: a ground plane and a few ruin walls with AI-painted textures, cards for distant trees | the painted look, made to measure | time; flat up close |
| A 3D artist | exactly the game's look | money |

Proposal (Q88): one stage from Poly Haven for floors 1–10, to judge the look before more.

## 7. Decisions (the owner, 2026-11-11: "do it", the proposals)

| Question | Answer |
|---|---|
| Q88 The stage's art | Poly Haven (CC0): the moonlit ruins for floors 1–10 first, to judge the look |
| Q89 How many stages | one per floor range (`"from"`, `"to"`), like the painted backdrops; a boss floor may have its own |
| Q90 2D fighters in 3D | projected boxes (B1); **since 2026-11-12 their sprites are drawn in the 3D pass** (the owner: dynamic light must light the 2D fighters; then: no card picture, the sprites themselves placed in 3D, §4.3) |
| Q91 How much the camera moves | subtle all the time; push-ins and orbits only for special moves, a boss's entrance and the last blow |
| Q92 Web and Android | the painted backdrop until the owner asks. **The web: on since 2026-11-11** (the owner: "turn it on"); Android still off |
| Q93 3D particles | the same `.particle` files, `"space": "3d"` emitters |

## 8. What is built (B1, B2)

| Part | Where |
|---|---|
| The stage builder: downloads the Poly Haven files into `Build/art_cache/polyhaven`, writes `assets/media/battle3d/moonlit_ruins/` (**1.2 MB**, the owner's budget 3 MB, 2026-11-11; was 16.9 MB): the night sky (Qwantani Moonrise) as `sky.jpg` (1536 px, gamma 2.2, values over 1 clipped: 51 KB, was a 4.7 MB `.hdr`), the scanned props simplified (`simplify_gltf.py`, numpy vertex clustering: the trunk 101,802 → 2,881 triangles, about 3,000 each), the fort's pieces 5,123 triangles, textures by kind (colour / normal / the rest: the fort and the ground 512 / 256 / 256 px, the props 256 / 256 / 128) as JPG 80, the fort's 6 pieces in one trimmed `fort_pieces.gltf` (each prop names its `"node"`), a 40 m ground of grassy cobblestone, the soft shadow disc | Each model is packed into one `.glb` (`pack_glb.py`: the JSON, buffer and textures in one file, nothing re-encoded): the stage is 10 files, 9 `.glb` and `sky.hdr`; `--loose` keeps the `.gltf` folders | `tools/battle3d/pack_glb.py` |
| `tools/battle3d/make_moonlit_ruins.py`, `simplify_gltf.py`. The stage: about 56,000 triangles, 71 MB of textures + 17 MB of sky on the GPU (was 290,000 and 150 MB) |
| The stage row (floors 1–10): sky, exposure 0.25, the moonlight, ground, shadow, 13 props, camera, marks, heights | `assets/data/battle.json` `"stages3d"` |
| The host draws the stage behind the sprites (GltfRenderer, the title's views). It is loaded **in the background as soon as the floor is entered** (reading, decoding, the sky's prefiltering and its half-float packing on a worker thread; only the GPU upload on the main thread) and kept between battles, so a battle opens at once. Measured 2026-11-11: Release 1.0 s in the background + 0.09 s upload; Debug 8 s + 1 s (it was 2 s / 17 s, all at the battle's start). The prefiltered sky is cached in `<save folder>/cache/sky_<hash>.env` (17 MB, made once per sky file): a later start in Debug has the stage 4 s after launch instead of 10. A battle that opens before the stage is ready shows darkness, never the painted backdrop, and every battle's stage fades in from black (0.45 s). A scripted run waits for it (the same picture every time; `--battle-3d-wait=off` tries the normal way) | `src/game/src/battle_stage.*`, `game_session.cpp` |
| The game picks the row by floor, places each fighter's box from its mark (feet and head projected; the layout's box gives only the shape), draws no painted backdrop under it | `game_combat.cpp` `f_Battle3D*`, `f_PlaceBattle3DBoxes` |
| The camera: the stage's shot (wider on a narrow screen), breathing, the special moves' `"camera"` block as a push in (`"move": "orbit"` + `"deg"`: also round and back; star_seal and tiger_wind have it), the first danger's close-up, a slow drift in during the last blow's slow motion, the shake as the camera's | `Game::f_Battle3DCamera`, `special_moves.json` |
| A special move's darkening: the stage's light, not a black veil | `Battle3DCamera::m_fDim` |
| Living light (2026-11-11, the owner: "it looks like a static 3D picture"): two braziers (Poly Haven stone fire pits) with a flickering warm point light each and their own fire (`stage_fire`: flame, embers, smoke; `tools/battle3d/make_stage_fx.py`), drawn at their place and scaled by the distance; clouds passing the moon (its light drifts 60–100 %); every battle effect in the scene lights the stage a moment at the nearest fighter (warm on a monster, red on the hero); the floor's motes and fireflies over the stage. battle.json `"fires"`, `"clouds"`, `"flash"`. The renderer takes the moon + 3 point lights (no shadows for them) | `Game::f_Battle3DLights`, `f_AddBattle3DFlash`, `f_DrawBattle3DStageFx`, `battle_stage.cpp` |
| A monster's special attack coming (its danger cells light): the camera goes low and close and looks up at it (it fills the screen), swings a little to the side so the depth shows, with a rumble, and is back before the hit so the cells can be read; the fight's first holds for the cells' intro. battle.json stage camera `"danger"` (zoom, height, lookAt, fovAdd, deg, ms, rumble, off); never with Reduce effects. A fighter behind the camera is not drawn | `Game::f_Battle3DDangerShot`, `f_Battle3DCamera` ("low") |
| **The web** (2026-11-11): the stage is on in browsers. The single-threaded build makes it on the main thread when the floor is entered (a short pause there, 0.33 s measured); the multithreaded one in a worker. The browser's sky is smaller (256 px faces, 16 samples) and not cached (no 17 MB in its storage); no shadows. Measured in headless Chrome on this PC's GPU: both builds load the stage and draw it (45 draws), every shader compiles. Any device: battle frames over 45 ms for most of 3 s and the painted backdrop comes back for the session (`[battle3d] too slow here`). `node tools/web_smoke_test.mjs <build> <out> --battle --gpu` checks it | `battle_stage.cpp`, `game_session.cpp`, `main_sdl.cpp`, `tools/web_smoke_test.mjs` |
| A node scaled to nothing draws nothing (the fort's hidden pieces): a stage frame is 45 draws + 41 shadow draws (was about 200); Debug GPU 16.6 → about 3 ms | `gltf_renderer.cpp` |
| On / off: `--battle-3d=on|off`; on in normal play on the desktop, off in scripted runs (the reference pictures) and on the web and Android; a scenario turns it on with `"battle3d": true` | `main_sdl.cpp`, `game_scenario.cpp` |
| The fighters' sprites in the scene (2026-11-12, §4.3): drawn in world groups (each pixel placed in metres at the fighter's feet, facing the camera), then by the stage with the models' shader: lit by the moon, the fires and the flashes, depth tested, alpha blended, sorted by distance; the braziers' fires the same way, unlit (the left flame is behind the archer's bow). Nothing is cut (a first try drew each fighter into a card picture: its edge cut the raised hand and the bow). Measured: at a monster's hit the archer's brightness goes 76 → 117 and turns red. battle.json stage `"sprites"` (`glow`, `off`); the stage is 10 files, 1.2 MB | `IRenderer::f_BeginWorld`, `BgfxRenderer::f_WorldGroups`, `Game::f_DrawBattle3DFighter` / `f_Battle3DSpriteMatrix`, `BattleStage::f_DrawSprites`, `GltfRenderer::f_DrawSprites` |
| Where the attack's prompts show (the owner, 2026-11-12/13): never on a monster, **over the hero's head** -- the bow's target, the sword's arrows and count, the staff's circles, a mash, in one area (420 × 120 design px, 160 high in portrait) centred over the hero, its bottom a quarter of his height over his box (his painted head reaches about a fifth above it), kept on the screen; the bow's first hint along its top. Each monster's "!", next move, name with HP and a short HP bar sit on its own head (`.fhead`, placed where it stands) | `Game::f_TouchArea`, `game_ui.cpp`, `battle.rml`, `battle.rcss` |
| A special attack's wind-up (2026-11-13, the owner): when its danger cells light, the monster **powers up** -- its clock waits 3.5 s while the camera stays close on it, it plays its "power" clip (crouch, trembling harder as it swells and pulses red, a roar at 3.05 s), `monster_power` particles and the `sfx.monster_power` rumble -- then **charges** until the hit (the looping "charge" clip, `monster_charge` energy drawn in every 0.45 s; the cells' 1.4 s warning), and the hit **goes off** with `monster_unleash`, its shout `sfx.monster_roar` and a strong shake. If it hurts the hero, he is **knocked down**: what he was doing stops, his `hurt_big` clip plays (thrown back, down on a knee, up again: 1.6 s; `tools/art/make_hero_rig2d.py`, every weapon), the attack panel says "受到重創", and no attack starts (the track, the weapon's play, the ultimate, the active) for `downMs` 1600; moving and guarding still work (event `heroDown`; test `scenario.battle_special_down`). The fight's first (the cells' intro) counts the intro in the 3.5 s. A stun loses the power-up. Every effect and sound a monster starts is bound to it (`CombatState::Foe::m_vFx`, `m_vSounds`; the owner: "monster should keep emitter on its class"), and when it falls each is checked by battle.json `"onFoeDeath"` (`fxRules`, `soundRules`: stop, fade or keep; the power-up and charge stop, the release and the roar play out): `Game::f_FoeEffectsOnDeath`, a project rule (CLAUDE.md "Battle effects belong to their monster"). A look without the clips plays "special" and idle. battle.json `feelFx` `"specialAttack"` (powerMs, powerFx, powerSound, chargeFx, chargeFxEveryMs, unleashFx, shout, shakePx, shakeMs, downMs, off); battle events `power`, `unleash`, `heroDown`. Clips by `tools/make_monster_battle_anims.py` and `tools/art/make_monster_rig.py`, particles by `tools/make_battle_fx.py`, sounds by `tools/audio/make_sfx.py` | `Game::f_StartFoePower`, `f_UnleashFoePower`, `f_FoeCharging`, `f_StepFoes` |
| Body types and the floor bosses (2026-11-13, the owner): every monster has `enemies.json` `"body"` 1-5 (1 small: slime, bat; 2 most; 3 big: golem, the toad; 4-5 the bosses), and battle.json `"bodyScale"` makes it its height on the 3D stage (1: 0.75, 2: 1, 3: 2, 4: 2.4, 5: 2.8 times the stage's 1.6 m). A boss is body 3 or more with the special moves `boss_near`, `boss_line`, `boss_fan` (moves.json; cells, power-up, charge, release, knock-down as the toad's). Ten bosses stand on the last tile of the way to the up stairs of F01-F10, the first floors (the owner: "make it F01-F10, I just want to test"; legend "X"): F01 Hivebear 3, F02 Fungolem 3, F03 Cragtortle 4, F04 Grimoox 4, F05 Gravewarden 3 (a special one kept small), F06 Hearthound 4, F07 Sphinxie 4, F08 Glacibear 5, F09 Astrowl 5, F10 Chronoknight 5; early-game numbers (HP 160 rising by 20 a floor to 340, ATK 20-30, DEF 2-5); the heavy blow too from F06. Vorkath, the final boss on F70, is body 5 with the boss moves too, and painted like them (`tools/art/make_boss_concept.py`: a crimson hooded demon lord; look `vorkath`). Each of the ten **guards its up stair** (`enemies.json` `"guardsStairs"`): the stair stays locked while it stands there unbeaten; bumping it says "{name} guards the stairs up", and beating it says the stairs are open (`Game::f_StairGuard`; test `scenario.stairs_boss_guard`). Art: each a team's third monster of `make_monster_concepts.py`, painted rigs by `tools/art/make_monster_rig.py` (one atlas and .anim each, `looks.json`), 32 px map pictures in `art_raw/sprites` (the game atlas rebuilt). Test `scenario.battle_floor_boss` | `Game::f_Battle3DHeight`, `enemies.json`, `moves.json`, `story/floors/F01-F10` maps |
| A monster's body scale on the 3D stage (2026-11-13, the owner): `enemies.json` `"scale3d"` (1 = the stage's height: `"heights"` `monster` 1.6 m, `big` 3.0 m) multiplies its height there; its picture, shadow and head block follow. The tutorial's thicketoad has 2 (3.2 m) | `Game::f_Battle3DHeight`, `assets/data/enemies.json` |
| Tests: `scenario.battle_3d` (the stage is drawn; the hero nearer: taller, feet lower), `smoke.battle_3d` (reference picture `tests/golden/battle_3d.png`, refreshed 2026-11-12 for the sprites in the scene) | `tests/` |

Not yet: the 2D particles and hit effects still play in screen space at the fighters' projected places, over the scene
(B3 moves them into 3D); the battle editor previews the painted backdrop (B5).
