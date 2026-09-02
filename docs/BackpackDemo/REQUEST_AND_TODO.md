# Backpack 系統需求與待辦

## 使用者這次要求的內容

1. 先檢查 TOMS 程式碼，修正不正確的地方
   - 錯誤邏輯
   - 不正確的程式碼
   - render 問題
   - node order 問題
   - 其他會影響顯示或互動的問題

2. 將背包系統整合進 TOMS
   - 遊戲內要有一個可開啟背包的按鈕
   - 背包系統要讓玩家能看到自己擁有的物品
   - 點選物品後要能看到名稱、說明、效果、icon
   - 要支援 mouse / touch / keyboard

3. 背包 UI 需求要接近傳統 RPG
   - 圖示 + 名稱 + 數量
   - 物品說明
   - 物品效果，例如 `STR+100`、`HP+100`
   - 物品操作選單：`Use / Drop / Close`

4. 建立一份可示範的 Web 背包 demo
   - 放在 `docs/BackpackDemo`
   - 使用 `item.json` 作為資料來源
   - 每個物品包含：
     - `gameId`
     - `name`
     - `icon`
     - `description`
     - `effect`
     - `type`
     - `stackable`
     - `category`

---

## 目前已完成

- [x] 建立 `docs/BackpackDemo/` demo
- [x] 建立 `item.json`
- [x] 建立圖示 `icons/*.svg`
- [x] 建立可瀏覽的 Web 背包 demo
- [x] demo 支援 mouse / touch / keyboard
- [x] demo 有 `Use / Drop / Close` 操作選單
- [x] TOMS web UI 已加入「背包」按鈕，可打開遊戲內背包
- [x] TOMS 啟動提示文字已更新
- [x] 清理了 `loadAssets()` 裡不必要的 `ifstream` 開檔

---

## 接下來還要做的事

- [ ] 把遊戲內背包 UI 再調整得更像傳統 RPG（例如更像格子背包、加右側詳細資訊區）
- [ ] 讓遊戲內背包的操作選單更接近 demo（可點選 Use / Drop / Close）
- [ ] 讓背包按鈕在更多情況下都可見且位置更明顯
- [ ] 在 Web build / Desktop build 重新編譯並驗證
- [ ] 若需要，將 demo 內容和遊戲內資料格式統一

---

## 目前的整合方向

- **Demo**：`docs/BackpackDemo/`
- **遊戲內開啟方式**：Web UI 右上角的「背包」按鈕
- **遊戲內資料來源**：`data/items.json`
- **遊戲內背包入口**：`Game::toggleInventory()`

---

## 備註

這份文件的目的，是把這次需求分成：

1. 已完成的 demo
2. TOMS 需要修正的程式碼
3. 遊戲內背包整合
4. 後續可再優化的 UI 項目

如果後面要繼續做，可以直接從這份清單往下完成。
