# 11 — 編輯器語言（English / 中文）

> 英文原文：[11_EDITOR_LANGUAGE.md](../11_EDITOR_LANGUAGE.md)

所有 Qt 編輯器（atlas_editor、anim_editor、particle_editor、font_editor、toms_editor）都可以用選單列最右邊的
**中文 / English** 按鈕切換英文與繁體中文。切換立即生效：選單、按鈕、標籤、提示、表格標題與清單選項會直接改變；含有數字的文字
（狀態列、數量）會在編輯器下次更新時改變。所有編輯器共用同一個選擇，並在下次啟動時保留。

網頁編輯器（`tools/web_editors`）有自己的語言設定（`settings.js` 的 `language`）。toms_editor 內的舊關卡編輯器同時顯示兩種語言，沒有切換。

## 運作方式

- 編輯器透過 Qt 的 `tr()` 以英文撰寫介面。`tools/studio_common/Language.h` 安裝一個翻譯器，從
  `tools/studio_common/i18n/zh_TW/*.json`（英文 → 中文，每個編輯器一個檔案，共用詞彙在 `common.json`）取得翻譯，
  這些檔案以 Qt 資源的形式編入編輯器。
- 切換時會走訪所有開啟的視窗，把每段文字換成另一種語言。
- 安裝 Qt 時若有 `qtbase_zh_TW.qm`，Qt 自己的對話框（檔案對話框按鈕）也會是中文。
- `--selftest`、`--headless` 與截圖執行一律以英文開始，因為它們的檢查會讀取介面文字。
  `TOMS_LANGUAGE=en` 或 `zh` 可強制指定語言，且不會改變儲存的選擇。

## 新增介面文字

在 `tr()` 中以英文撰寫新的介面文字，並把它的中文加入該編輯器的 JSON 檔：

```json
 "Delete %n sprite(s)?": "要刪除 %n 個精靈圖嗎？",
 "&File": "檔案(&F)",
```

翻譯中要保留 `%1`、`%2`、`%n` 與 `&` 快速鍵字母。用 `tr()` 寫的鍵盤快速鍵（`"Ctrl+O"`）對應到它自己。

`python tools/studio_common/i18n/i18n_check.py` 會列出還沒有翻譯的 `tr()` 文字（`--stale` 也會列出已沒有人使用的翻譯）。
有缺少的翻譯時 `studio.i18n_complete` 測試會失敗，`studio.language` 則在一個小視窗上檢查切換功能本身。
