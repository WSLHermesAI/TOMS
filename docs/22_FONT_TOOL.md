in 

# 22 — Picture fonts (font tool)

Score and counter text in the old slot projects (copies in `tests/font`: `connect_score.fnt`, `total_score.fnt`, …)
is not drawn from a TTF: every character is a hand-made picture, combined into one PNG page and described by a BMFont
text `.fnt`. The font tool combines **pictures you add, each given its character,** into the same pair, and draws a
test text with it. Step-by-step usage: [23_FONT_TOOL_GUIDE.md](23_FONT_TOOL_GUIDE.md)
(中文：[zh_TW/22](zh_TW/22_FONT_TOOL.md), [zh_TW/23](zh_TW/23_FONT_TOOL_GUIDE.md)).

| Part            | Where                                  | What                                                                                                                                                                                                    |
| --------------- | -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| runtime         | `src/core/engine/bitmap_font.h/.cpp` | `BitmapFont`: parses `.fnt` (the BMFont "Arial" layout and convert2fnt-web), lays UTF-8 text out, makes `Quad`s for `IRenderer`; `FontText`: load / setText / setPosition / setAlign / render |
| core            | `tools/font/core/font_build.h/.cpp`  | pictures → page + `.fnt`; a font → pictures; the preview renderer                                                                                                                                   |
| `fontpack`    | `tools/font/cli`                     | the command line: build, split, preview, info                                                                                                                                                           |
| `font_editor` | `tools/font/qt`                      | the window: add pictures, type each one's character, test text with a live preview, save                                                                                                                |
| tests           | `tools/font/tests`, `tests/font`   | `font.tool`, `font.editor_selftest`                                                                                                                                                                 |

## 1. The input: pictures and their characters

One PNG per character (`fonttool::GlyphPicture`: the image + its code point). In `font_editor` the pictures are
added one by one or many at once, and each one's character is typed in its row; `checkGlyphs` lists the pictures
without a character or with one an earlier picture has, and nothing is built until there are none. The character
is guessed from the file name where it is one (`charFromName`): `0.png`, `K.png`, `中.png`; `dot.png`,
`comma.png`, `space.png`, `slash.png`, `times.png` (×) and the other names for characters a file name cannot hold;
`U+00D7.png`. `fontpack build` takes the pictures as files, with the characters from the file names or from
`--chars "0123456789.,KM"` in order; a folder argument stands for its PNGs and its `chars.txt` (`file=char` lines,
also the order).

Each picture is used **as it is**: its width is the advance and its height is the cell. Pictures of different
heights sit on the bottom of the line. A fully transparent picture gives only an advance (a space). Without a space
picture, a space is added. The space's width (`spaceAdvance`, default `defaultSpaceAdvance`: a space picture's width,
else the `1` picture's, else 5) and the line height (`lineHeight`, the step of `\n`; default `defaultLineHeight`:
the tallest picture) can be set; the glyphs stay on the tallest picture's baseline (`base`). The page order is the list
order.

`glyphsFromFont` turns a finished font back into pictures (the space: a blank picture as wide as its advance), which
is how the editor opens a `.fnt` to change it. Offsets other than the bottom alignment are not kept; none of the
old fonts has any.

## 2. The output

`NAME.fnt` + `NAME.png`. The `.fnt` text is always the convert2fnt-web form of `tests/font/connect_score.fnt`
(its `info face="…"` is the font name, a label nothing reads). Where the pictures go on the page is
`BuildOptions::layout` (`PageLayout`):

| Layout            | `--layout` | Page                                                                                                                                                                                                                                                                   |
| ----------------- | ------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Row` (default) | `row`      | the pictures side by side on one row from (1, 1), 1 px apart, wrapping at the max size: the old fonts' layout. `font.tool` checks that the pictures in `tests/font/connect_score_pictures` rebuild `connect_score.fnt` byte for byte and its PNG pixel for pixel |
| `Tight`         | `tight`    | the atlas tool's packer, `atlas::packPages` (MaxRects, every placement rule tried, the smallest page, then squeezed): 1 px border and gaps, sides up to the max size                                                                                                 |
| `TightPow2`     | `pot`      | the same with `PackSettings::powerOfTwo`: both sides powers of two (`connect_score`: 1024 × 128)                                                                                                                                                                  |

The layout only moves pictures on the page: `font.tool` checks that the test text draws pixel for pixel the same
with every layout, that no two glyphs touch and that every glyph keeps its pixels. Pictures that do not fit in the
max size are an error.

## 3. fontpack

```bat
fontpack build   a.png b.png c.png --chars "0.," --out assets\media\fonts\score.fnt [--layout row|tight|pot] [--face F] [--space N] [--line-height N] [--advance-pad N] [--max-width N]
fontpack build   art\0.png art\1.png art\dot.png --out assets\media\fonts\score.fnt   :: characters from the file names
fontpack preview tests\font\connect_score.fnt --text "12,345.6K" --out preview.png [--scale 0.5] [--bg 0F0F1A]
fontpack split   tests\font\total_score.fnt --out art\total_score   :: a font -> pictures + chars.txt
fontpack info    tests\font\connect_score.fnt
```

Exit codes: 0 ok, 1 warnings (a skipped file, characters the text uses but the font lacks), 2 errors (a picture
without a character or with a repeated one, a file that cannot be read), 3 bad command line.
`font_editor --headless <command>` runs the same commands.

## 4. font_editor

`font_editor [a.png b.png … | font.fnt]`. A table of the pictures: picture, **Character** (editable: one character,
a name such as `space` / `dot`, or `U+XXXX`), code, size, source. **Add pictures…** (or PNGs dropped on the window),
**Remove**, **Up** / **Down** (page order), **File → New**. Red Character cells are the `checkGlyphs` problems; they
are listed under the table and block Save. The font is rebuilt in memory on every change, from the rows that are
fine, so the preview always shows what is there. **Test text**: the preview redraws on every key; Enter starts a new
line; characters the font lacks are skipped and counted. View: scale, background (checker, the game's background
colour, black, white). **Combined picture** (`PageView`): the built page fitted to the pane, its size and "power of
2" when it is, every glyph outlined; the selected row's glyph is highlighted (the rest dimmed), and a click on a
glyph selects its row. Font: font name (the `face`, a label only), page layout (one row / tight / tight power of
2), page max size, space width, line height (both *auto* with their value shown), letter spacing. **Save font as…** writes `.fnt` + `.png`. **Open font (.fnt)** replaces the rows with the font's glyphs (`glyphsFromFont`);
saving it unchanged gives the same `.fnt`.

The preview draws through `BitmapFont::layout`, the same code the game lays text out with, with nearest sampling
like the game's sprites.

## 5. Drawing it in the game

```cpp
FontText score;
score.load(renderer, "assets/media/fonts/connect_score.fnt");   // once
score.setText("12,345");                                          // in update / an event
score.setPosition(1200, 40);
score.setAlign(BitmapFont::Align::Right);
score.render();                                                   // in render, between begin() and end()
```

`FontText` (`bitmap_font.h`) reads the `.fnt` and its page through `vfsReadAll` (desktop, web, Android), uploads the
page with `IRenderer::loadTexture`, and keeps its quads until text, position, alignment, scale or colour change, so
`render()` every frame is cheap. Also `setScale`, `setColor`, `setVisible`, `width`/`height`, `missingChars`,
`release`. A copy of a loaded `FontText` shares the font and the texture. The full table is in
[23_FONT_TOOL_GUIDE.md](23_FONT_TOOL_GUIDE.md) section 5. Underneath, `BitmapFont` has `layout()`, `measure()` and
`appendQuads()` for custom drawing.

A letter missing in one case falls back to the other: the old fonts have either `K M` or `k m`. `bitmap_font.cpp`
is part of the game's `toms_core` (`src/game/CMakeLists.txt`), so game code includes `bitmap_font.h` and uses
`FontText` directly. The tools build it on their own as `toms_bitmap_font` (`tools/font/CMakeLists.txt`, with
`stb_image_impl.cpp` for the page decode); never link `toms_bitmap_font` and `toms_core` into one program (both
define stb_image).

## 6. Tests

- `font.tool` (`font_tool_test tests/font <scratch>`): the runtime's parsing and layout (offsets, kerning, scale,
  alignment, new lines, UTF-8, the old BMFont layout, case fallback); file names; the pictures of
  `tests/font/connect_score_pictures` → `connect_score` byte for byte; split → build; wrapping, spacing and
  `chars.txt`; pictures with characters set by hand, missing and repeated characters, a font opened back into
  pictures; every `.fnt` in `tests/font` loads, draws 0–9 and opens as pictures; the tight and power-of-2 layouts
  (glyphs apart, pixels kept, power-of-2 sides, the text drawn the same as with one row); `FontText` on a fake renderer
  (load, set*, render, copies, release); and the test text `12,345.6K / 9,999M 0` drawn at scale 0.5, compared
  with `tests/font/preview.png`. If a change to the rendering is intended, run it once with `TOMS_UPDATE_GOLDEN=1`,
  look at the new `preview.png`, and commit it.
- `font.editor_selftest`: `font_editor -platform offscreen --selftest tests/font/connect_score_pictures <outdir>`.
  It adds the pictures, adds one with no character and checks Save is refused, types `%`, a repeated `7` and
  `U+0025`, removes and reorders rows, selects a row (its glyph highlighted on the combined picture) and clicks a
  glyph (its row selected), switches to the power-of-2 layout and saves it, types test texts, scales, saves, opens
  the saved font and saves it again (the
  same `.fnt`), and runs `--headless info`. It also leaves `window.png` and `preview.png` in the outdir to look at.

`tools/test_select.py` runs both for any change in `tools/font`, `tests/font` or `bitmap_font.*`.
