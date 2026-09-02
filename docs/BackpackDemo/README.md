# BackpackDemo

這是一個給 TOMS 用的**傳統 RPG 背包系統示範**，支援：

- 滑鼠點選
- 觸控點選
- 鍵盤方向鍵 / Enter / Space / Esc
- 道具詳情面板
- `Use / Drop / Close` 快捷操作
- `item.json` 驅動的道具資料
- 每個道具都有 `gameId / name / icon / description / effect`

## 參考的傳統 RPG 背包樣式
我整理的常見元素如下：

- 道具清單以**圖示 + 名稱 + 數量**呈現
- 右側或下方顯示**詳細說明 / 效果數值**
- 按下道具後跳出**操作選單**
- 大型按鈕與清楚的選取框，方便觸控與滑鼠
- 將角色狀態放在旁邊，讓玩家能立即看到裝備或消耗後的變化

我做這個 demo 時，參考了常見 RPG/inventory UI 的結構，例如：

- 精簡的裝備清單與資料面板
- 大按鈕的行動選單
- 以 icon + 說明文字讓玩家快速理解道具用途

## 檔案

- `index.html`：主頁
- `style.css`：外觀
- `app.js`：互動邏輯
- `item.json`：道具資料
- `icons/`：道具圖示

## 如何開啟

在這個資料夾中執行：

```bash
python3 -m http.server 8080
```

然後開啟：

```text
http://localhost:8080/
```

## item.json 格式

每個道具包含：

- `gameId`：道具 ID
- `name`：名稱
- `icon`：圖示檔名
- `description`：詳細說明
- `effect`：對玩家屬性的影響，例如 `{ "str": 100, "hp": 100 }`
- `type`：裝備 / 消耗品 / 任務道具
- `stackable`：是否可堆疊
- `category`：分類
