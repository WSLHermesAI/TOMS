# 24 — Editor language (English / 中文)

Every Qt editor (atlas_editor, anim_editor, particle_editor, font_editor, toms_editor) switches between English
and Traditional Chinese with the **中文 / English** button at the right end of its menu bar. The switch is
immediate: menus, buttons, labels, tool tips, table headers and list choices change in place; text with numbers
in it (status lines, counts) changes the next time the editor updates it. The choice is shared by all the
editors and kept between runs. 中文：[zh_TW/24_EDITOR_LANGUAGE.md](zh_TW/24_EDITOR_LANGUAGE.md).

The web editors (`tools/web_editors`) have their own language setting (`settings.js`, `language`). The old stage
editor inside toms_editor shows both languages side by side and has no switch.

## How it works

- The editors write their UI in English through Qt's `tr()`. `tools/studio_common/Language.h` installs a
  translator that answers from `tools/studio_common/i18n/zh_TW/*.json` (English → 中文, one file per editor,
  `common.json` for words they share), built into the editors as a Qt resource.
- Switching walks every open window and swaps each text for its other language.
- Qt's own dialogs (file dialog buttons) are Chinese when Qt's `qtbase_zh_TW.qm` is installed with Qt.
- `--selftest`, `--headless` and screenshot runs always start in English, because their checks read UI text.
  `TOMS_LANGUAGE=en` or `zh` forces a language and leaves the saved choice alone.

## Adding UI text

Write new UI text in English inside `tr()`, and add its Chinese to the editor's JSON file:

```json
 "Delete %n sprite(s)?": "要刪除 %n 個精靈圖嗎？",
 "&File": "檔案(&F)",
```

Keep `%1`, `%2`, `%n` and the `&` shortcut letter in the translation. Keyboard shortcuts written with `tr()`
(`"Ctrl+O"`) map to themselves.

`python tools/studio_common/i18n/i18n_check.py` lists `tr()` text that has no translation yet (`--stale` also lists
translations nothing uses any more). The `studio.i18n_complete` test fails while one is missing, and `studio.language`
checks the switch itself on a small window.
