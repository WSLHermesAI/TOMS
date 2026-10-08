// settings.js -- stage editor settings. Edit, save, then reload the page (F5).
// 關卡編輯器設定。修改並存檔後，重新整理頁面（F5）。
//
// A size chosen in the editor itself (the 🔍 list, or Alt + = / Alt + -) is remembered by the browser and
// wins over uiScale here; Alt + 0, or "settings.js" in the 🔍 list, goes back to this file's value.
// 在編輯器中選的大小（🔍 清單或 Alt + = / Alt + -）會被瀏覽器記住，並優先於這裡的 uiScale；
// 按 Alt + 0，或在 🔍 清單選「settings.js」，就會回到這個檔案的值。
const EDITOR_SETTINGS = {
  uiScale: 1.25,   // whole interface: 1 = 100 %, 1.5 = 150 %, 2 = 200 % (0.75 … 2.5)   整個介面大小
  mapZoom: 2,      // map tiles: 1 = 32 px per tile, 2 = 64 px (0.5 … 4; mouse wheel over the map, keys + / -)   地圖格子大小（地圖上滾動滑鼠滾輪）
  language: '',    // '' = follow the browser, or 'en' / 'zh_TW'   介面語言
};
