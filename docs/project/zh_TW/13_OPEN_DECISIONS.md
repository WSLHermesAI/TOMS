> 英文原文：[13_OPEN_DECISIONS.md](../13_OPEN_DECISIONS.md)

# 13 — 待決事項：擁有者還沒決定的事

2026-11-04 對照各文件檢查。本頁只是集中列出；細節仍在每列連結的文件中。**運作方式**（專案規則，`CLAUDE.md`）：

1. **您**在 **我的回答** 欄寫下想要的做法，寫在本檔或英文版都可以，用什麼字句都可以。文件 14 第 5 節的問題也有同樣的欄位。
2. **您**請 Claude 檢查決定。
3. **Claude** 讀兩個版本與文件 14 第 5 節。不清楚、不完整或與其他回答衝突的，會再問您。
4. **所有回答都清楚後，Claude 會問您是否要開始做**，得到您同意才開始。
5. 決定並開工後，答案寫進該列連結的文件，並從這裡刪除該列。

**我的回答** 空白表示尚未決定：該列不會有任何動作，其提案也不會套用。

## 1. 卡住的：您決定之前不會動工

| 問題 | 我的提案 | 等它的工作 | 位置 | 我的回答 (My Answer) |
|---|---|---|---|---|
| **戰鬥公式：** 自由運算式（`max(1, a.atk - t.def / 2)`），還是固定的勇者鬥惡龍式公式加上可調數字？ | 尚無 | 共用的戰鬥規則（11 §4.5） | [11](../../gameplay/zh_TW/11_BATTLE_DATA.md) §5 O1、O2 |  |
| **升級：** 除了點數，每級所有屬性 +1 是否保留，還是只用點數？ | 兩者都保留 | 平衡 | [11](../../gameplay/zh_TW/11_BATTLE_DATA.md) §5 O3 |  |
| **Q37 技能：** 戰鬥前選兩個欄位，還是維持目前一個主動按鈕？您在文件 17 的筆記說每個熟練度有 5 招，在戰鬥畫面中選擇，這兩個選項都不是這樣。 | 那 5 招，在戰鬥中選，不用欄位（2026-11-04 由「兩個欄位」改成這樣） | R6 | [15](../../gameplay/zh_TW/15_BATTLE_REVAMP.md) §9、[17](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) §6 |  |
| **Q31 刷寶：** 獨立模式，還是塔的最後一層以下？能自動玩嗎？ | 從 hub 進入的獨立模式，可開關自動戰鬥 | R7 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q32 寶箱：** 用什麼開：新道具、金幣或水晶？ | 新道具「寶箱鑰匙」 | R7 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q33 裝備加成：** 單件、整套，還是兩者？ | 兩者 | R7 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q59 雙手劍（14 第 2.5 節）：** 這個記憶玩法取代文件 17 的 V 形滑動，還是只用在技能與絕招？ | 取代，成為普通攻擊 | 雙手劍 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q60 雙手劍：** 每次滑對就立刻以各自的倍數攻擊，還是全部滑完才以最後的倍數打一下？ | 每次滑對立刻攻擊：×1.2、×1.4、×1.6 | 雙手劍 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q61 雙手劍：** 滑錯或時間到時，已打出的攻擊算數，還是整個攻擊失敗？ | 已打出的算數；連段到此結束 | 雙手劍 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q62 雙手劍：** 各等級的 N 是多少，方向有幾個（4 或 8）？N × X 是共用的時間，還是每次滑動各有 X 秒？ | Lv 1 時 N = 3，最多 6；先用 4 方向；共用時間 | 雙手劍 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q63 雙手劍：** 您的短筆記寫三個提示、每個 0.3 秒、一秒內畫完。這和第 2.5 節是同一個玩法，只是回答時間改成 1 秒而不是 N × X（0.9 秒）嗎？回答時方向會隱藏嗎？是分開滑動，還是一筆畫完？ | 同一個玩法：N 從 3 開始，回答時間放資料（N = 3 時 1 秒），方向隱藏，分開滑動 | 雙手劍 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §2.5 |  |
| **Q64 難度曲線：** 您筆記的順序是第三層、第五章、第七層。依 14 段、每段 5 層（Q47），第五章會是 F21–F25，在第七層之後。它是第五章，還是第五層？ | 第五章，F21–F25：五連擊需要練到那個等級；第三層與第七層仍是樓層 | W10 | [17](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) §11、§18 |  |
| **Q65 史詩與傳說任務：** 您的筆記寫每個武器；文件 17 寫成每個系列。那是 6 個或 24 個任務。 | 每個系列，因為招式來自系列的熟練度 | W8 | [17](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) §6、§18 |  |
| **Q66 看到抗性：** 您的筆記說怪獸的說明可以標記，打倒後卡片也會寫；文件 17 寫成持有卡片後才有。在那之前說明會顯示嗎？ | 不會：在那之前玩家從傷害得知（打下去不痛） | W5 | [17](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) §10、§18 |  |
| **Q67 裝備與抗性：** 您的筆記說玩家要切換武器跟裝備；文件 17 只有武器。裝備在這裡有什麼作用？戰鬥中能換嗎？ | 裝備只能在戰鬥之間更換；裝備可以帶有對某種傷害類型的加成 | W5 | [17](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) §10、§18 |  |
| **Q69 戰鬥中的海洋：** 試過 `ocean_viewer` 後，要放到戰鬥背景嗎？用哪種外觀？戰鬥者需要站在東西上（甲板、木筏、岩石），還是站在水面上？ | 要，前景放一片船的甲板讓戰鬥者有地方站；外觀用您在檢視器儲存的設定 | 海洋背景 | [engine 21](../../engine/zh_TW/21_OCEAN_VIEWER.md) §7 |  |
| **Q22 測試情境：** 放在哪裡共用（雲端儲存、Google Drive、自己的伺服器）？ | 雲端儲存（R2） | 下載（W39） | [09](../../gameplay/zh_TW/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q23 上傳：** 每位開發者各自的金鑰，還是共用一把？ | 各自的金鑰 | 下載（W39） | [09](../../gameplay/zh_TW/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q24 DLC：** 內容包能修改現有內容，還是只能新增？ | 只能新增 | DLC（W39） | [09](../../gameplay/zh_TW/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q25 DLC：** 用我們的儲存空間還是商店的 DLC？付費或免費？ | 我們的儲存空間，先免費 | DLC（W39） | [09](../../gameplay/zh_TW/09_DOWNLOADS_AND_DLC.md) §7 |  |
| **Q26 團隊流程：** 狀態板仍寫「等擁有者說明」，但 [11_TEAM_PLAN](11_TEAM_PLAN.md) 已經存在。那份計畫就是答案嗎？ | 等您 | W40 | [狀態板](../progress_report/PROGRESS_REPORT.md)、[團隊計畫](11_TEAM_PLAN.md) |  |
| **影像關卡，問題 2：** 每個事件一個 id，還是幾個「事件欄位」id？ | 每個事件一個 id | 影像關卡 | [04](../../gameplay/zh_TW/04_IMAGE_STAGES.md) §6 |  |
| **影像關卡，問題 3：** 70 層一次轉換，還是編輯到哪層才轉？ | 一次轉換 | 影像關卡 | [04](../../gameplay/zh_TW/04_IMAGE_STAGES.md) §6 |  |
| **發行語言：** 哪些是必要的？這決定未翻譯文字算警告或錯誤。（Q51 只決定「目前」：zh_TW 與英文。） | 警告 | 發行 | [事件編輯器 02](../../gameplay/event_editor/02_EDITOR_DESIGN.md) §6 |  |
| **設計地圖的整理：** 要不要做、順序為何？包含決定章節節拍。都不影響目前的工作。 | 不做 | 程式結構 | [19](../../engine/zh_TW/19_DESIGN_MAP.md) |  |
| **刪除 `combat.json` 與 `story.json`？** 沒有程式讀取它們。 | 保留 | 清理 | [11](../../gameplay/zh_TW/11_BATTLE_DATA.md) §5 O4、[19](../../engine/zh_TW/19_DESIGN_MAP.md) |  |
| **3D 戰鬥者 view：** 之後要做，還是放棄？您說過「之後再試 3D」；設為 `"3d"` 的外觀目前退回 2D。 | 不做 | 3D 戰鬥者 | [18](../../gameplay/zh_TW/18_FIGHTER_VIEWS.md) |  |

## 2. 已用我的提案做進遊戲：要改才需回答

這些沒有得到回答，所以照提案做了。沒有工作在等它們。

| 問題 | 遊戲目前的做法 | 位置 | 我的回答 (My Answer) |
|---|---|---|---|
| **Q27** 新玩法取代點數軌，還是附加？ | 逐武器取代；舊玩法保留為設定 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q28** 弓：拖曳或按住？目標會動嗎？ | 往後拖再放開 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q29** 劍：方向怎麼顯示？ | 顯示後隱藏（之後由文件 16 重做） | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q30** 魔法陣從戰鎚移到法杖嗎？ | 法杖有魔法陣；戰鎚尚未做 | [14](../../gameplay/zh_TW/14_WEAPON_PLAY_AND_LOOT.md) §5 |  |
| **Q34** 打擊感多強？ | 中等，在 `battle.json`，並有減少特效的設定 | [15](../../gameplay/zh_TW/15_BATTLE_REVAMP.md) §9 |  |
| **Q35** 評等與 S 的獎勵？ | S / A / B / C；S 給 +20 % 金幣與較高的卡片機率 | [15](../../gameplay/zh_TW/15_BATTLE_REVAMP.md) §9 |  |
| **Q36** 畫面配置？ | 玩法在怪物上方；防禦條僅為選項 | [15](../../gameplay/zh_TW/15_BATTLE_REVAMP.md) §9 |  |
| **Q38** 改版先做哪一步？ | 先做 R1 + R2（R1–R5 已完成） | [15](../../gameplay/zh_TW/15_BATTLE_REVAMP.md) §9 |  |
| **Q39** 劍的輸入時間？ | 每 1 秒的最後 0.5 秒 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q40** 法杖：點錯圓圈？ | 算失誤；玩法繼續 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q41** 弓的蓄力上限？ | 滿蓄力時箭自動射出 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q42** 「!」內的防禦等級？ | 前 40 % 是格擋 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q43** 技能怎麼使用？ | 先把星印做成法杖的大招；技能清單之後再做 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q44** 訓練場會消耗卡片，可以嗎？ | 每次 1 張、每天 3 次、×2 經驗與武器使用次數 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q45** 招式來不及完成？ | 給部分傷害：完成數 / 總數 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q46** 攝影機特寫？ | 每招在資料中設定；設定可縮短它 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §9 |  |
| **Q68** 特殊防禦：您的筆記是跟怪物綁定；放在招式上可以嗎？ | 放在招式上（`moves.json` `guardPlay`），巫妖與礦石魔像在用 | [16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) §4.3、§9 |  |

## 3. 不是問題，但在等您

| 事項 | 位置 | 我的回答 (My Answer) |
|---|---|---|
| **您對調整後英雄外觀的判斷**（弓、劍、法杖） | `Build/art_out/hero_*/gif/`、[20](../../engine/zh_TW/20_CHARACTER_ART.md) §5 |  |
| **怪物美術：** 概念圖與選用結果。`tools/art/make_monster_concepts.py`（16 隊共 48 隻）尚未提交 | `Build/art_out/monster_concepts/teams.png` |  |
| **下一步做什麼：** 文件 17 的順序 W1–W10 | [17](../../gameplay/zh_TW/17_WEAPONS_AND_PLAYS.md) |  |
| **在遊戲中試玩法**，並說說手感 | [15](../../gameplay/zh_TW/15_BATTLE_REVAMP.md)、[16](../../gameplay/zh_TW/16_SKILLS_AND_PLAYS.md) |  |
| **`tools\test.cmd all`：** C++ 改名後還沒執行；大部分測試被關閉 | [08](08_TESTS.md) |  |
| **`smoke.battle` 參考圖：** 等您滿意戰鬥畫面後再製作 | [08](08_TESTS.md) |  |
| **發佈 Web 版**（需要您的 GitHub 登入，W18） | [07](07_PUBLISH_GITHUB_PAGES.md) |  |

狀態板其餘的 **Waiting to finish** 清單（W 編號）大多是其他裝置上的測試，不是決定：
[progress_report/PROGRESS_REPORT.md](../progress_report/PROGRESS_REPORT.md)。

**因已定案而未列入：** [06](06_BUILD_ANDROID.md) §6 的問題（已照選項 B 實作），以及事件編輯器 02 §6 的樓層清單問題（它依賴的產生器已移除，Q6）。
