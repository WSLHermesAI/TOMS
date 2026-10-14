# 23 — Font tool user guide

How to make picture fonts with the font tool. The technical reference for programmers is
[22_FONT_TOOL.md](22_FONT_TOOL.md). 中文：[zh_TW/23_FONT_TOOL_GUIDE.md](zh_TW/23_FONT_TOOL_GUIDE.md).

## What it does

You add the pictures of your characters (one PNG per character), give each picture its character, and the tool
combines them into a picture font: a `.fnt` file plus one `.png` page. It is the same format as the old score fonts
in `tests/font` (`connect_score.fnt`, `total_score.fnt`, …: BMFont text, convert2fnt-web layout), so old fonts and
fonts made with this tool are interchangeable, and an old font can be opened, changed and saved again.

It comes in two forms:

| Tool | Use it for | Where |
| --- | --- | --- |
| `font_editor` | Making a font by hand: add pictures, set each one's character, type a test text and watch it render, save | `Build\windows-release\bin\font_editor.exe` |
| `fontpack` | Scripts and batch work: build from pictures, preview to PNG, split a font into pictures, print info | `Build\windows-release\bin\fontpack.exe` |

Both are built with the rest of TOMS (`tools\build.cmd`).

## 1. The pictures

One PNG per character, from anywhere on disk; they do not have to be in one folder.

- Each picture is used as it is, not trimmed: its **width is the advance** (how far the next character starts) and
  its height is the cell. Leave the spacing you want inside the picture, or add it later with Letter spacing.
- Pictures of different heights are aligned on the **bottom** of the tallest picture.
- A fully transparent picture draws nothing and only gives an advance. Use one to set the width of a space.
- The **space** is always in the font. Its width is, by default, the width of a space picture if you added one,
  else the width of the `1` picture, else 5 px. Set **Space width** to change it.
- The **line height** (how far a new line, `\n`, moves down) is, by default, the tallest picture's height. Set
  **Line height** to change it; the glyphs stay where they are on each line.
- The glyphs go on the page in the order of the list.

**The character of a picture** is typed in the editor. To save typing, it is guessed from the file name when the name
is a character:

| File name | Guessed character |
| --- | --- |
| `0.png` … `9.png`, `K.png`, `x.png`, `中.png` | the character itself |
| `dot.png`, `comma.png`, `space.png` | `.` `,` and space |
| `colon.png`, `slash.png`, `backslash.png`, `star.png`, `question.png`, `quote.png`, `less.png`, `greater.png`, `pipe.png` | `:` `/` `\` `*` `?` `"` `<` `>` `\|` (characters Windows forbids in file names) |
| `plus.png`, `minus.png`, `percent.png`, `dollar.png`, `hash.png`, `times.png` (×), `yen.png`, `euro.png` | other common symbols |
| `U+00D7.png` | any Unicode code point |
| anything else (`digit_zero.png`, `score_07.png`) | nothing: type it in the editor |

The same words work in the editor's Character column: type `7`, `space`, `dot` or `U+0025`.

## 2. Make a font in font_editor

Start `font_editor.exe`. You can also pass pictures or a `.fnt` on the command line
(`font_editor a.png b.png`, `font_editor tests\font\connect_score.fnt`), or drop them on font_editor.exe in Explorer.

1. **Add pictures…** (button, File menu or Ctrl+O): pick one or more PNG files. You can also drag PNG files from
   Explorer onto the window. Each becomes a row: its picture, **Character**, code, size and source file.
2. **Give every picture its character:** double-click the **Character** cell and type it (`7`, `K`, `%`, `space`,
   `dot`, `U+00D7`). A cell guessed from the file name can be changed the same way. A **red** cell has no character,
   or one that an earlier row already has. The rows above the options list the problems, and Save is refused until
   they are fixed.
3. Arrange the list: **Up** / **Down** set the order on the page, **Remove** deletes the selected row, **File →
   New** clears everything.
4. Type in the **Test text** box on the right. The preview redraws on every key, and on every change to the list;
   Enter starts a new line. Characters the font does not have are skipped, and the count is shown under the
   preview.
5. Adjust the view if needed: **Scale** (0.1–8) and **Background** (checker, the game's background colour, black,
   white).
6. Check the **Combined picture** under the preview: the page exactly as it will be saved, with its size (and
   "power of 2" when it is). **Select a row** and its glyph is outlined in yellow on the picture, the rest dimmed;
   **click a glyph** on the picture and its row is selected in the list.
7. Adjust the font if needed (the preview and the combined picture update):
    - **Font name (label only)**: the name written into the `.fnt` file's first line (`info face="…"`). BMFont calls
      it the *face*: the name of the typeface the font was made from. For a picture font it is only a label: the
      game and the tools never read it. Leave it empty to use the file name.
    - **Page layout**: how the pictures are combined into the page.
        - *One row (as convert2fnt-web)*: side by side, 1 px apart, like the old fonts. A long font wraps into
          more rows at the max size.
        - *Tight (atlas packer)*: the atlas editor's packer (MaxRects) finds the smallest page that holds all the
          pictures, 1 px apart.
        - *Tight, power of 2 (atlas packer)*: the same, with the page's width and height powers of two (64, 128,
          256, 512, 1024, …), for platforms or compression that need them. `connect_score` becomes 1024 × 128.
      The layout changes only where the pictures sit on the page: text draws exactly the same.
    - **Page max size**: one row wraps past this width; the tight layouts keep both sides within it (default
      4096). Pictures that do not fit are an error.
    - **Space width**: how far a space moves the next character. *auto* (shown with its value): a space
      picture's width, else the `1` picture's width, else 5. A value set here wins over a space picture; the
      lowest value (*auto*) goes back to the default.
    - **Line height**: how far a new line moves down. *auto* (shown with its value): the tallest picture's height.
      Raise it for more room between lines, lower it to pack them closer. An opened font keeps its line height.
    - **Letter spacing**: pixels added to every glyph's advance (negative tightens).
8. **Save font as…** (Ctrl+S) and choose `NAME.fnt`. The tool writes `NAME.fnt` and `NAME.png` side by side.

**Change an existing font:** **File → Open font (.fnt)** turns every glyph of the font back into a row (the space
as a blank picture as wide as its advance). Add, remove, reorder or re-type characters, then save. Opening a font
and saving it unchanged gives the same `.fnt` again.

The preview is drawn by the same layout code the game uses, with the same nearest-pixel sampling, so what you see is
what the game shows.

## 3. fontpack on the command line

`fontpack` has four commands. `font_editor --headless <command>` runs the same commands.

| Command | What it does |
| --- | --- |
| `fontpack build <a.png> <b.png> … --out NAME.fnt` | Combines the pictures, in the order given, into `NAME.fnt` + `NAME.png` |
| `fontpack preview <font.fnt> --text TEXT --out FILE.png` | Draws a test text with a font into a PNG |
| `fontpack split <font.fnt> --out <folder>` | The reverse of build: every glyph as a picture, plus `chars.txt` |
| `fontpack info <font.fnt>` | Lists the face, line height, page and every glyph |

Options:

| Option | Command | Meaning | Default |
| --- | --- | --- | --- |
| `--chars TEXT` | build | The characters of the pictures, in order (`"0123456789.,KM"`); without it, the file names | file names |
| `--layout row\|tight\|pot` | build | Page layout: one row; tight (atlas packer); tight with power-of-2 sides | `row` |
| `--face NAME` | build | Font name written into the `.fnt` (a label only) | the output file name |
| `--space N` | build | Space width in pixels | a space picture's width, else the `1` picture's, else 5 |
| `--line-height N` | build | How far a new line moves down, in pixels | the tallest picture |
| `--advance-pad N` | build | Pixels added to every advance (letter spacing) | 0 |
| `--max-width N` | build | Page max size: one row wraps past it; tight layouts keep both sides within it | 4096 |
| `--scale S` | preview | Draw size (0.5 = half) | 1 |
| `--pad N` | preview | Empty pixels around the text | 8 |
| `--bg RRGGBB` or `RRGGBBAA` | preview | Background colour; `0F0F1A` is the game's | transparent |

Examples:

```
fontpack build art\digit_a.png art\digit_b.png art\digit_c.png --chars "012" --out assets\media\fonts\score.fnt
fontpack build tests\font\connect_score_pictures --layout pot --out assets\media\fonts\connect_score.fnt
fontpack build art\0.png art\1.png art\dot.png --out assets\media\fonts\score.fnt
fontpack preview tests\font\connect_score.fnt --text "12,345.6K" --out preview.png --scale 0.5 --bg 0F0F1A
fontpack split tests\font\total_score.fnt --out art\total_score
fontpack info tests\font\connect_score.fnt
```

A folder name in `build` stands for all the PNGs in it (with its `chars.txt`, `file=char` lines, for their
characters and order), which is how `split` output is built again.

Exit codes, for scripts: **0** ok · **1** warnings (a skipped file, or text characters the font lacks) · **2**
errors (a picture without a character, a character given twice, a file that cannot be read) · **3** bad command
line.

## 4. Common tasks

**Make a new score font from artwork**

1. Export each character from the art file as its own PNG, all the same height, with the spacing drawn in.
2. In `font_editor`, **Add pictures…** and select them all.
3. Type each picture's character where it was not guessed (`0`–`9`, `.`, `,`, `K`, `M`, …). Fix any red rows.
4. Type real values in the test box, such as `1,234,567`, `12.5K` and `9.99M`. If the characters sit too far apart
   or too close together, fix the pictures or use Letter spacing.
5. **Save font as…**

**Change or extend an old font** (e.g. add `%` to `connect_score`)

1. In `font_editor`, **File → Open font (.fnt)** → `tests\font\connect_score.fnt`.
2. **Add pictures…** → the `%` picture, and type `%` in its Character cell.
3. **Save font as…** → where the game loads it.

Or with the command line: `fontpack split tests\font\connect_score.fnt --out art\connect_score`, add
`art\connect_score\percent.png`, then `fontpack build art\connect_score --out <where it ships>\connect_score.fnt`.

**Check text before shipping**

`fontpack preview my.fnt --text "<the longest value>" --out check.png`. Exit code 1 means some characters of the text
are not in the font.

## 5. Drawing the font in the game

Use `FontText` (in `src/core/engine/bitmap_font.h`): one object per piece of text on screen.

```cpp
#include "bitmap_font.h"

FontText score;                                   // a member of your scene / HUD class

// once, when the scene starts
score.load(renderer, "assets/media/fonts/connect_score.fnt");
score.setPosition(1200, 40);
score.setAlign(BitmapFont::Align::Right);

// in update, or when the value changes
score.setText("12,345");

// in render, between renderer.begin() and renderer.end()
score.render();
```

| Function | What it does |
| --- | --- |
| `load(renderer, "x.fnt")` | Reads the `.fnt` and its PNG and uploads the page. Returns false if a file is missing. |
| `setText("...")` | The text, UTF-8; `"\n"` starts a new line. Setting the same text again costs nothing. |
| `setPosition(x, y)` | Where the text goes, in screen pixels: `y` is the top of the first line, `x` the left edge, centre or right edge depending on the alignment. |
| `setAlign(BitmapFont::Align::Left / Center / Right)` | Right is the usual choice for scores, so the digits grow to the left. |
| `setScale(0.5f)` | Draw size (1 = the pictures' size). |
| `setColor(r, g, b, a)` | Multiplies the pictures; `a` fades the text. White `(1, 1, 1, 1)` draws them as they are. |
| `setVisible(false)` | Hides the text without clearing it. |
| `render()` | Draws it. Cheap every frame: the layout is redone only after a change. |
| `width()`, `height()` | The text's size at the current scale, for placing other things next to it. |
| `missingChars()` | How many characters of the text the font has no picture for. |
| `release()` | Frees the page texture when the scene closes. |

**Several labels with the same font:** load one `FontText`, then copy it. A copy shares the font and its texture
(no second upload) and keeps its own text, position and colour. Call `release()` once, when none of them draws any
more.

```cpp
FontText total = score;           // same font, own text
total.setPosition(640, 300);
total.setAlign(BitmapFont::Align::Center);
total.setText("1,000,000");
```

A letter missing in one case uses the other: the old fonts have either `K M` or `k m`, never both.

`FontText` is part of the game's core library (`toms_core`): any game code can include `bitmap_font.h` and use it.
For lower-level work there is `BitmapFont` itself: `layout()`, `measure()`, `appendQuads()`.

## 6. Troubleshooting

| Problem | Cause and fix |
| --- | --- |
| A red Character cell / "no character given" | Type the picture's character in its Character cell (or with `fontpack`, name the file after it or use `--chars`). |
| "… is already given to …" | Two pictures have the same character. Change or remove one. |
| "'…' is not a character" | The Character cell takes one character, a name (`space`, `dot`, `comma`, …) or `U+XXXX`. |
| A character is missing in the preview | The font has no picture for it. The count is shown under the preview, and `fontpack preview` exits with 1. |
| Characters overlap or have gaps | The advance is the picture's width. Crop or pad the pictures, or set Letter spacing / `--advance-pad`. |
| Space is too wide or too narrow | Set Space width / `--space`. |
| Lines too far apart or too close | Set Line height / `--line-height`. |
| Small glyph sits too low or high | Pictures are bottom-aligned. Give every picture the full line height, with the character placed where it should sit. |
| "only one page fonts are supported" | The `.fnt` uses several PNG pages, which this tool does not read. |
| "the pictures do not fit on one … page" | A tight layout needs a larger page: raise Page max size / `--max-width`. |
| The page must be a power of 2 | Choose the page layout *Tight, power of 2* / `--layout pot`. |
