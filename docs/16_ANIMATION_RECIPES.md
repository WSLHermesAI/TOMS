# 16 · Animation recipes: writing simple `.anim` clips by hand (or by AI)

How to write fade in / fade out, simple moves and short battle moves (step in, swing, step back)
directly as `.anim` JSON, without the editor. The rules are written so that a person or an AI can
follow them step by step. The full format and the editor are covered in
[15_ANIMATION.md](15_ANIMATION.md).

Every clip below is in [examples/anim_recipes.anim](examples/anim_recipes.anim), a working file
that is checked by `ctest` (`anim.check_recipes`). Copy from it.

## 1. Workflow

1. Write or extend a `.anim` file (JSON, UTF-8). Put it near its atlas: atlas paths are stored
   **relative to the `.anim` file**.
2. Check it. This must print `0 error(s)`; exit code 0 = ok, 2 = errors:
   ```
   Build\windows-release\bin\anim_editor.exe --headless check path\to\file.anim
   ```
3. Watch it:
   - In the game: `toms_game --anim=path/to/file.anim#clipName` plays the clip centred on the
     screen and logs its events (`[anim] event hit`).
   - In the editor: `anim_editor path/to/file.anim`, then pick the clip and press Space.
4. Adjust times, values and eases, then check again.

## 2. Format cheat sheet

```jsonc
{
  "version": 1,
  "atlases": ["../../assets/media/atlas/game.atlas"],   // relative to this file; first = lookup order
  "clips": [
    {
      "name": "attack",          // unique in the file; the game plays clips by name
      "length": 0.8,             // seconds; 0 or missing = up to the last key
      "playCount": 1,            // 1 = once (default), N = N times, -1 = loop forever
      "stayAtLastFrame": true,   // after the end: keep the last frame (default) or hide (false)
      "root": { /* node */ }
    }
  ]
}
```

**Node.** Every field is optional except `name`. The defaults are shown.

| Field | Default | Meaning |
|---|---|---|
| `name` | — | unique among its siblings; use readable names (`body`, `weapon`, `slash_fx`) |
| `sprite` | `""` | the sprite name from the atlas (`"slime"`, or `"game:slime"` with an atlas id); `""` = an invisible group |
| `pos` | `[0, 0]` | position in pixels relative to the parent; **y goes down** |
| `rot` | `0` | degrees; **positive = clockwise** on screen; not wrapped (0 → 720 = two turns) |
| `scale` | `[1, 1]` | negative x mirrors (`[-1, 1]` = face the other way) |
| `color` | `[1, 1, 1, 1]` | r, g, b, a from 0 to 1, multiplied with the sprite; children are multiplied too |
| `visible` | `true` | `false` hides the node **and its children** |
| `pivot` | the atlas's pivot (centre) | `[0..1, 0..1]` of the sprite, measured from its top-left; it is the point the node rotates and scales around |
| `order` | `0` | draw order among siblings (higher = in front); `< 0` = behind the parent's own sprite |
| `blend` | `"normal"` | `"add"` = additive (glows, flashes, slashes) |
| `inheritColor` | `true` | `false` = ignore the parents' colour/fade |
| `tracks` | none | the keys, one array per channel (below) |
| `children` | none | child nodes: they move, turn, scale and fade with this node |

**Tracks.** Each channel has its own key array. Include **only the channels that change**: a node
that only fades has only `color`.

| Track | `v` value | Between keys |
|---|---|---|
| `pos` | `[x, y]` | interpolated with the key's `ease` |
| `rot` | `degrees` | interpolated |
| `scale` | `[x, y]` | interpolated |
| `color` | `[r, g, b, a]` | interpolated |
| `sprite` | `"name"` | jumps to the new sprite at the key (flipbook frames) |
| `visible` | `true` / `false` | jumps at the key |
| `event` | `"name"` | sent to the game once, when the time passes the key |

**Key:** `{ "t": seconds, "v": value, "ease": "name" }`. The `ease` is optional (default
`"linear"`) and sets the curve **from this key to the next one**, so the last key's ease is
ignored.

**Eases:** `linear`, `stepped` (hold the value, then jump at the next key), and
`quadratic`, `cubic`, `quartic`, `quintic`, `sinusoidal`, `exponential`, `circular`, `bounce`,
`elastic`, `back`, each with `In`, `Out` or `InOut` added to the end (`quadraticOut`,
`backInOut`, ...). A cubic Bézier `[x1, y1, x2, y2]` (like CSS `cubic-bezier`) also works.

| You want | Use |
|---|---|
| a start that is quick then slows down (dash in, slide in, pop) | `quadraticOut`, `cubicOut` |
| a slow start that speeds up (wind-up, fall, fade out) | `quadraticIn`, `cubicIn` |
| a smooth move back and forth (bob, return to place) | `sinusoidalInOut`, `quadraticInOut` |
| an overshoot then settle (pop-in, squash recovery, a weapon's follow-through) | `backOut` |
| a bounce on landing | `bounceOut` |
| an instant change | `stepped`, or two keys very close together |

## 3. Rules (the mistakes people make)

1. **Before its first key, a track holds the first key's value, not the rest value.** To keep
   something hidden until 0.15 s, start its `visible` track with `{ "t": 0, "v": false }`.
   The same applies to `color`: begin a fade-in with an alpha-0 key at `t: 0`.
2. **After its last key, a track holds the last key's value.** To end where it started, end on
   the starting value (for example, step back to `[0, 0]`).
3. **To hold still in the middle, use two keys with the same value** (`[28, 0]` at 0.15 and at
   0.45), or give the first key `"ease": "stepped"`.
4. **Keys in one track have different times and go in increasing order.** A time cannot appear
   twice in one track; to make a sudden jump, put the two keys 0.01 s apart.
5. **Keep every key within `length`.** The check warns about keys past the clip's length.
6. **Children move with their parent.** Put a weapon or an effect under the body, and its `pos`
   is then relative to the body. Give a weapon a `pivot` at its handle (for example `[0.5, 0.9]`)
   so that it swings around the hand.
7. **A fade changes only alpha.** Keep r, g, b at 1, unless you want a tint such as a red hit
   flash (`[1, 0.3, 0.3, 1]`).
8. **Sprite names must exist in the atlas.** The game atlas
   (`assets/media/atlas/game.atlas`, 32×32 sprites) has: `bat boss_demonlord coin demon
   door_blue door_red door_yellow exp_up floor gem_atk gem_def golem key_blue key_red key_yellow
   npc_handmaiden npc_king npc_princess npc_sorcerer npc_villager player potion_blue potion_red
   scroll skeleton slime stairs_down stairs_up wall wraith`. It has no weapon or slash sprites
   yet, so the examples use `gem_atk` in their place. Swap in the real sprites once the atlas
   has them.
9. **Timing:** at 60 fps one frame is about 0.016 s. Battle moves read well at 0.3–1.0 s in
   total. A strike itself is 0.08–0.15 s, and a recovery is about twice as long as the strike.
10. **Events are names, not code.** Use names agreed with the game code (`hit`, `sfx_slash`,
    `shake`). The game receives them from `AnimPlayer::takeEvents()` on the frame the time
    passes the key.

## 4. Recipes

Each recipe is one node under the clip's `root`. Only the `tracks` are shown; the full clips are in
[examples/anim_recipes.anim](examples/anim_recipes.anim).

**Fade in** (`fade_in`, 0.5 s):
```json
"color": [ { "t": 0.0, "v": [1, 1, 1, 0], "ease": "quadraticOut" },
           { "t": 0.5, "v": [1, 1, 1, 1] } ]
```

**Fade out** (`fade_out`, 0.5 s, `"stayAtLastFrame": false`): use the same two keys in reverse
with `quadraticIn`.

**Slide in from the left** (`slide_in`, 0.4 s). The node moves and fades in at the same time:
```json
"pos":   [ { "t": 0.0, "v": [-64, 0], "ease": "cubicOut" }, { "t": 0.4, "v": [0, 0] } ],
"color": [ { "t": 0.0, "v": [1, 1, 1, 0] },                  { "t": 0.2, "v": [1, 1, 1, 1] } ]
```

**Idle bob** (`idle_bob`, 1 s, `"playCount": -1`). The node ends where it started, so the loop is
seamless:
```json
"pos": [ { "t": 0.0, "v": [0, 0],  "ease": "sinusoidalInOut" },
         { "t": 0.5, "v": [0, -3], "ease": "sinusoidalInOut" },
         { "t": 1.0, "v": [0, 0] } ]
```

**Hop with a squash on landing** (`hop`, 0.45 s):
```json
"pos":   [ { "t": 0.0,  "v": [0, 0],   "ease": "quadraticOut" },
           { "t": 0.18, "v": [0, -16], "ease": "quadraticIn" },
           { "t": 0.36, "v": [0, 0] } ],
"scale": [ { "t": 0.36, "v": [1, 1],     "ease": "quadraticOut" },
           { "t": 0.4,  "v": [1.2, 0.8], "ease": "backOut" },
           { "t": 0.45, "v": [1, 1] } ]
```

**Pulse / pop** (`pulse`, 0.4 s): scale `[1,1]` → `[1.3,1.3]` (`quadraticOut`) → `[1,1]`
(`backOut`).

**Blink** (`blink`, 0.6 s): `visible` keys alternating `false` / `true` every 0.1 s, ending on
`true`.

**Spin** (`spin`, 1 s, loop): `rot` from `0` to `360`, linear.

**Flipbook** (`flipbook`, loop): `sprite` keys every 0.15 s
(`key_yellow` → `key_blue` → `key_red`). For a frame sequence in the editor, select the frames
in a Sprites tab and drop them on the scrubber: the editor asks for the time gap
([15](15_ANIMATION.md)).

**Hurt** (`hurt`, 0.3 s): a red flash that fades back to white while the node shakes left and
right, getting smaller each time:
```json
"pos":   [ { "t": 0.0, "v": [0, 0] }, { "t": 0.05, "v": [-4, 0] }, { "t": 0.1, "v": [4, 0] },
           { "t": 0.15, "v": [-3, 0] }, { "t": 0.2, "v": [2, 0] }, { "t": 0.25, "v": [0, 0] } ],
"color": [ { "t": 0.0, "v": [1, 0.3, 0.3, 1], "ease": "quadraticIn" }, { "t": 0.3, "v": [1, 1, 1, 1] } ]
```

**Die** (`die`, 0.6 s, `"stayAtLastFrame": false`): a red flash, then the node fades out
while it squashes flat (scale to `[1.4, 0.2]`).

## 5. Battle example: step in, swing, step back (`attack`)

The attacker faces right (the target is at +x). Total 0.8 s.

| Time (s) | `body` | `weapon` (child of body) | `slash_fx` (child of body, additive) |
|---|---|---|---|
| 0.00 → 0.15 | steps forward `[0,0]` → `[28,0]`, `quadraticOut` | hidden | invisible (alpha 0) |
| 0.15 → 0.20 | holds at `[28,0]` | appears, raised back at `-100°` (wind-up) | |
| 0.20 → 0.32 | holds | swings `-100°` → `80°`, `cubicOut` | 0.22–0.26 flashes in, grows |
| 0.28 | **event `hit`** (the target reacts: damage number, `hurt` clip) | | |
| 0.32 → 0.45 | holds (follow-through) | stays down at `80°` | 0.26–0.40 fades out, grows to 1.6× |
| 0.45 → 0.75 | walks back `[28,0]` → `[0,0]`, `quadraticInOut` | hidden from 0.5 | |
| 0.75 → 0.80 | rest | | |

```json
{
  "name": "attack",
  "length": 0.8,
  "root": {
    "name": "attacker",
    "children": [
      {
        "name": "body",
        "sprite": "player",
        "tracks": {
          "pos": [
            { "t": 0.0,  "v": [0, 0],  "ease": "quadraticOut" },
            { "t": 0.15, "v": [28, 0] },
            { "t": 0.45, "v": [28, 0], "ease": "quadraticInOut" },
            { "t": 0.75, "v": [0, 0] }
          ],
          "event": [ { "t": 0.28, "v": "hit" } ]
        },
        "children": [
          {
            "name": "weapon",
            "sprite": "gem_atk",
            "pos": [12, 4],
            "pivot": [0.5, 0.9],
            "order": 1,
            "tracks": {
              "visible": [ { "t": 0.0, "v": false }, { "t": 0.15, "v": true }, { "t": 0.5, "v": false } ],
              "rot": [
                { "t": 0.15, "v": -100 },
                { "t": 0.2,  "v": -100, "ease": "cubicOut" },
                { "t": 0.32, "v": 80 }
              ]
            }
          },
          {
            "name": "slash_fx",
            "sprite": "gem_atk",
            "pos": [30, 0],
            "order": 2,
            "blend": "add",
            "inheritColor": false,
            "tracks": {
              "color": [
                { "t": 0.22, "v": [1, 1, 1, 0] },
                { "t": 0.26, "v": [1, 1, 1, 1], "ease": "quadraticOut" },
                { "t": 0.4,  "v": [1, 1, 1, 0] }
              ],
              "scale": [
                { "t": 0.22, "v": [0.6, 0.6], "ease": "quadraticOut" },
                { "t": 0.4,  "v": [1.6, 1.6] }
              ]
            }
          }
        ]
      }
    ]
  }
}
```

Why it is built like this:

- **The weapon and effect are children of `body`,** so they come along on the step forward and
  back with no extra keys.
- **The weapon's `pivot` is near its bottom** (the handle), so `rot` swings it around the hand.
  Negative angles lean it back (anticlockwise); positive angles bring it down in front.
- **`visible` starts with `false` at `t: 0`** (rule 1), so the weapon is not shown before the
  swing.
- **The hold keys (`[28,0]` at 0.15 and 0.45)** keep the body still during the swing (rule 3).
- **`hit` comes at 0.28,** just after the swing's fastest part. `cubicOut` covers most of the
  angle early, so the impact feels like it lands on the hit.
- **`slash_fx` uses `inheritColor: false` and `blend: add`,** so it glows on its own.

**Variations**

| Variation | Change |
|---|---|
| Attack to the left | Give the root `"scale": [-1, 1]`. That mirrors the whole clip, positions and swing included. |
| A heavier blow | A longer wind-up (hold `-100` until 0.3), a faster swing (0.08 s), and a short `body` recoil `[24,0]` right after `hit`. |
| A lunge / thrust | No `rot` track. Move the weapon's `pos` from `[8,4]` to `[24,4]` and back. |
| The target's reaction | A second clip for the target, played when the game receives `hit`: `hurt` (shake + red flash), or `die`. |
| Magic cast | `body` has a small `hop`. A `spell_fx` child above the head `pulse`s and fades out (`blend: add`). Send `"cast"` at the peak. |

## 6. Checklist (give this to an AI along with the request)

- [ ] Only the tracks that change, and every key array sorted by `t` with no repeated time.
- [ ] Every value a node has before its first key is right (`visible: false` / alpha 0 at `t: 0`
      where needed).
- [ ] Every clip ends where the next one expects it (the rest pose for one-shots; the same as the
      start for loops).
- [ ] `length` covers the last key; `playCount` / `stayAtLastFrame` are set for loops and
      one-shots that disappear.
- [ ] Every sprite name exists in the atlas, and every event name is one the game handles.
- [ ] `anim_editor --headless check file.anim` → `0 error(s)`; watched once with
      `toms_game --anim=file.anim#clip`.

**Prompt template:**

> Read docs/16_ANIMATION_RECIPES.md and docs/examples/anim_recipes.anim. Add a clip `<name>` to
> `<file>.anim`: `<what happens, in order, with rough timing>`. Sprites: `<names>`. Send the event
> `<name>` at `<moment>`. Follow the rules and the checklist, run `--headless check`, and fix
> anything it reports.
