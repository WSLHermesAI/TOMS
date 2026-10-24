# Tutorial: the UI editor

This walks you through changing the game's screens with the UI editor, one step at a time. You work on a **practice
screen** first, a small card made only for this tutorial, so nothing in the game can break. At the end you change a
real screen, the battle.

You need no programming. Everything here is what an artist or designer does: colours, sizes, positions, text, icons.
Showing new game information or adding new buttons needs the programmer (step 7).

The reference for every button is [08_RMLUI.md](08_RMLUI.md) "The UI editor". In the editor, **Help > UI Editor
Tutorial** (F1) opens this page.

---

## 0. Words used here

| Word | What it means |
|---|---|
| **Screen** | One page of the game's interface: the title, the battle, the store. Each is two files in `assets\media\ui`. |
| **`.rml`** | What is on a screen and in what order: boxes, text, pictures, buttons. Like HTML. |
| **`.rcss`** | How it looks: colours, sizes, positions, fonts. Like CSS. |
| **Element** | One thing on the screen: `<div>…</div>` is a box, `<span>` a piece of text in a line, `<img>` a picture. |
| **id / class** | Names you give elements so a style can find them. `id="card"` is one element's own name; `class="line"` can be shared by many. |
| **Rule** | One style: a **selector** saying which elements, then the **properties** in `{ }`. `#card { left: 312px; }` moves the element whose id is `card`. |
| **Design pixels** | Every screen is laid out on a 1024 × 768 page, then scaled to the window. `left: 312px` means 312 of those 1024. |
| **Binding** | `{{…}}` in an `.rml`: a value the game fills in, for example the player's gold. |

Keys you will use:

| Key | Does |
|---|---|
| Ctrl+S | Save the file (nothing on disk changes before this) |
| Ctrl+I | Inspect on / off: a click in the game picks an element instead of pressing it |
| F5 | Start the preview again |
| F8 | RmlUi's own debugger over the game |
| F1 | This tutorial |

---

## 1. Open the editor and the practice screen

1. Start the editor: `Editor\UiEditor.cmd` (or `Build\windows-release\bin\ui_editor.exe`).
2. **Help > Open Practice Screen.** The game on the right starts a new game, and a card appears over the map:
   *Traveller's Notice*, with a coin row, a potion row, a line with your gold, and an *Accept* button.
3. `practice.rcss (tutorial)` opens in the middle. The list on the left has every screen file of the game, and at the
   bottom the two practice files: `practice.rml` and `practice.rcss`. They are in `docs\examples\ui_tutorial`, not in
   the game's folder: the game itself never shows them.

The window: **files** on the left, **the text** in the middle, **the game** on the right, **Problems** and
**Inspector** at the bottom. The toolbar's **Preview** says what the game shows (now *Practice screen (tutorial)*).

---

## 2. Change a colour

1. In `practice.rcss` find the line:
   ```css
   #card-title { font-size: 30px; color: #f9e9b1; margin-bottom: 14px; }
   ```
   The colour `#f9e9b1` is drawn on its own colour, so you can see it in the text.
2. Change `#f9e9b1` to `#7fd1ff`.
3. Stop typing. After a moment the card's title in the game turns light blue.
4. The file in the list now has a ● in front: changed, not saved. The file on disk is still the old one.

Colours are `#rrggbb` (red, green, blue, each `00`–`ff`), or `#rrggbbaa` with transparency at the end (`cc` = 80 %
visible).

---

## 3. Move and resize the card

1. Find:
   ```css
   #card { position: absolute; left: 312px; top: 190px; width: 400px; padding: 24px 28px; }
   ```
2. Change `top: 190px` to `top: 120px`: the card moves up.
3. Change `width: 400px` to `width: 460px`: it gets wider. `left` is where its left edge is.
4. `padding` is the space inside the card's edge: `24px 28px` means 24 above and below, 28 left and right.

To centre a box of width *w*, its `left` is (1024 − *w*) ÷ 2. For 460 that is 282.

---

## 4. Change the text size

1. Find `.line { height: 42px; line-height: 42px; font-size: 22px; }`.
2. Make `font-size: 26px`. The two rows' text grows. Each row stays 42 pixels tall (`height`), and the text sits in its
   middle (`line-height` the same as `height`).
3. Rows too tight now? Make `height` and `line-height` both `48px`.

---

## 5. Add a row with an icon

1. Open `practice.rml` (click it in the list).
2. Find the potion row:
   ```html
   <div class="line"><img sprite="potion_red"/><span class="label">Red potion</span><span class="value">x 2</span></div>
   ```
3. Copy the line, paste it under, and change it to a yellow key:
   ```html
   <div class="line"><img sprite="key_yellow"/><span class="label">Yellow key</span><span class="value">x 1</span></div>
   ```
4. A third row appears, with the key's picture. It has the same look as the others because it has the same classes.

`sprite="…"` is a picture from the game's atlas, by its name. The names are the PNG names in `art_raw\sprites`
(without `.png`), or the lines of `assets\media\atlas\game.atlas`. A name that does not exist shows nothing.

---

## 6. Style one element only

The rows share `class="value"`, so a change to `.value` changes all of them. To make one different, give it a second
class:

1. In `practice.rml`, in the key row, change `class="value"` to `class="value rare"`.
2. In `practice.rcss`, add a new line at the end:
   ```css
   .rare { color: #ff8a65; }
   ```
3. Only the key's `x 1` turns orange.

Selectors you will meet:

| Selector | Picks |
|---|---|
| `#card` | the element with `id="card"` |
| `.line` | every element with `line` in its `class` |
| `.line img` | every `<img>` inside a `.line` |
| `.btn:hover` | a `.btn` while the mouse is over it |
| `div` | every `<div>` |

When two rules set the same property, the more exact one wins (`#id` over `.class` over a tag). Otherwise the later one
wins.

---

## 7. Show something from the game

1. In `practice.rml` find `<div id="you">{{hud.res_line}}</div>`. The game fills in `{{hud.res_line}}`: the gold,
   crystals and keys line of the map's top bar.
2. Change it to `<div id="you">{{hud.hp_text}}</div>`: the line now shows the hit points.
3. The values a screen can show are listed in `src\core\game\ui\ui_state.h` (`hud.…` for the map, `battle.…` for the
   battle, and so on), and each screen uses those of its own part.

**What needs the programmer:**

- a value that is not in that list;
- a new button that does something (`data-event-click`);
- showing or hiding by a game condition (`data-if`);
- a list made from game data (`data-for`).

You can still place and style these once they exist. Keep the `data-…` attributes as they are when you change the
look.

---

## 8. Find what styles an element (Inspect)

1. Press **Ctrl+I** (or *Inspect* in the toolbar). Clicks in the game now pick, they do not press.
2. Click the **Accept** button in the game.
3. `practice.rml` opens at the button's line. The **Inspector** at the bottom says `div#accept.btn.btn-yes`, and lists
   the rules that name it:
   - `common.rcss` `.btn { … }` (every button's shape);
   - `.btn-yes { … }` (the green);
   - `practice.rcss` `#accept { … }` (this button's size).
4. Double-click `#accept` in the Inspector: `practice.rcss` opens at that rule. Make `height: 56px; line-height: 56px;`.
5. Press **Ctrl+I** again to give the clicks back to the game.

`common.rcss` is shared by every screen of the game: change it only when you mean to change all of them.

For the whole picture (every element, every value it ends up with, the boxes it takes), **F8** opens RmlUi's own
debugger over the game; F8 again closes it.

---

## 9. When something is wrong (Problems)

1. In `practice.rcss`, misspell a property: change `color` in `.rare` to `colr`.
2. The key's number goes back to yellow (the rule is ignored), and **Problems** at the bottom lists RmlUi's message
   about it. A message that names a file and line opens there on a double-click.
3. Fix the spelling: the message goes away with the next change.

A screen that shows nothing at all usually has a missing `>` or `"` in the `.rml`; Problems says where RmlUi stopped.

---

## 10. Put the practice screen back

The practice files are part of the repository; leave them as they were for the next person.

1. With `practice.rcss` open: **File > Revert File**. Then the same for `practice.rml`.
2. The ● marks go away, and the card is back as it was.

If you saved them by mistake, ask the programmer, or in Git: `git checkout -- docs/examples/ui_tutorial`.

---

## 11. Now a real screen: the battle

1. **Preview**: pick `battle — battle_bow`. The game opens a battle with the bow.
2. Open `battle.rcss`. The battle's title is:
   ```css
   #title  { position: absolute; left: 262px; top: 14px; font-size: 38px; color: #ffcaa8; }
   ```
   Try `font-size: 44px` and watch the title in the game.
3. **Language**: pick 日本語 or Español. The game's text changes language (only here: the game's own setting stays).
   Check that long lines still fit; a line that runs into the next one needs more room (a taller box, or a smaller
   font).
4. Happy with it? **Ctrl+S**. Now the file on disk changes, and any running game (TomsEditor, a debug build) shows it
   too.
5. Not happy? **Revert File**.
6. Commit the `.rcss` / `.rml` you changed (see the team plan, [36_TEAM_PLAN.md](36_TEAM_PLAN.md) §4).

**Rules for the real screens:**

- Do not rename or remove an `id`, a `class` or a `data-…` attribute the game uses: the game finds its parts by them.
  Change the look in the `.rcss` instead. When in doubt, ask the programmer.
- New text that the player reads goes into `assets\data\text.json` (all languages), not into the `.rml`; the screen
  then shows it through a binding. That is the designer's and programmer's part.
- After new characters appear in a screen (a new symbol, new Chinese text), run `python tools\make_ui_font.py`.
- Check the screen in the languages with long words (Spanish, Japanese) before saving.

---

## 12. When something looks wrong

| Problem | Why, and what to do |
|---|---|
| Nothing changes in the game while typing | The change previews after a short pause. Still nothing? See Problems; the rule may be ignored (a misspelled property, a missing `;`) |
| The practice card is not there | Pick *Practice screen (tutorial)* in Preview (or Help > Open Practice Screen); press F5 to start it again |
| A picture shows as nothing or a white box | The sprite name does not exist in the atlas, or the `.rml` does not link `_atlas.rcss` |
| Clicks in the game do nothing | Inspect is on (Ctrl+I to turn it off) |
| The game's window is black | The graphics could not start (the editor needs a Direct3D 11 graphics card, like TomsEditor); the status bar says why |
| My change is in the editor but not in the game I run separately | It is not saved yet (●). Ctrl+S |
| A file changed on disk while I edited it | The status bar says so. Your text is kept; Revert File takes the one on disk |
