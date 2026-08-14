#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Generate a Traditional Chinese review PDF for the Tower of the Sorcerer (Vulkan C++) project.
Embeds rendered frames + A4 text pages via PIL rasterization (WenQuanYi Zen Hei)."""
import os, io
from PIL import Image, ImageDraw, ImageFont

ROOT = "/home/fatming/tower_vulkan"
FRAMES = ["frame01_stage1","frame02_dialogue_elder","frame05_combat_start","frame08_boss_combat"]
FONT = "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"
OUT = os.path.join(ROOT, "REVIEW_魔法塔_VulkanC++.pdf")

# A4 at 96 dpi -> 794x1123
PW, PH = 794, 1123
M = 48

def font(sz):
    return ImageFont.truetype(FONT, sz)

def wrap(text, fnt, maxw):
    lines=[]; cur=""
    for ch in text:
        if ch=="\n": lines.append(cur); cur=""; continue
        t=cur+ch
        if fnt.getlength(t) > maxw: lines.append(cur); cur=ch
        else: cur=t
    lines.append(cur)
    return lines

def text_page(title, body, draw_img=True):
    img=Image.new("RGB",(PW,PH),"white")
    d=ImageDraw.Draw(img)
    d.rectangle([0,0,PW,6],fill=(40,30,70))  # accent bar
    y=M
    t_f=font(26); d.text((M,y),title,font=t_f,fill=(20,20,30)); y+=40
    d.line([M,y,PW-M,y],fill=(180,180,190),width=1); y+=18
    f=font(16)
    for para in body:
        for ln in wrap(para,f,PW-2*M):
            d.text((M,y),ln,font=f,fill=(30,30,40)); y+=24
        y+=8
    return img

# ---- pages ----
pages=[]

pages.append(text_page("魔法塔：巫師之塔 — Vulkan C++ 實作驗收報告", [
 "專案目標",
 "以使用者先前建立的 Vulkan C++ 遊戲引擎為基礎，實作《魔法塔：巫師之塔》(Tower of the Sorcerer) 的 2D 地城爬塔遊戲：包含 10 個關卡、劇情對話系統、自動回合制戰鬥（玩家與敵人互毆）、道具與經驗成長。全部以 Vulkan 渲染，並於無顯卡環境 (lavapipe / LLVM pipe ICD) 離屏渲染驗證。",
 "",
 "技術架構",
 "• 渲染：Vulkan 1.x，離屏 (offscreen) COLOR_ATTACHMENT + TRANSFER_SRC，讀回為 PNG。",
 "• 2D 精靈：所有圖素打包成圖集 (atlas)，以 uniform grid 取 UV；文字另建 CJK 字型圖集 (444 字)。",
 "• 管線：單一 sprite 管線，頂點屬性 = 位置/目標矩形/UV 矩形/著色，push constant 傳解析度。",
 "• 資料：關卡、敵人、道具、對話皆為 JSON，與渲染解耦。",
 "• 戰鬥：自動回合制，玩家先攻，敵人反擊；擊敗獲得 EXP/金幣，達標升級；死亡返回本層。",
]))

pages.append(text_page("驗收結果 — 已通過項目", [
 "1. 引擎啟動：Vulkan 裝置初始化、交換鏈無關的離屏渲染管線建置成功 (EXIT=0)。",
 "2. 圖集上傳：27 個精靈 + CJK 字型圖集經 HOST_VISIBLE 線性圖像直接寫入，GPU 可見。",
 "3. 關卡渲染：第 1 關以 13×11 網格繪製地板/牆壁/樓梯/門/鑰匙，並放置玩家、史萊姆、道具、NPC 精靈。",
 "4. HUD：標題「魔法塔 Tower of the Sorcerer」、樓層「村莊外緣 (1/10)」、HP/ATK/DEF/LV/EXP/金幣/鑰匙 完整顯示。",
 "5. 對話系統：NPC 長老以繁體中文敘事（「年輕人，災禍起源於十年前：黑巫師 Vorkath 封印了安穩之星，並將公主囚於塔頂」），含分支選項「我該怎麼做？」「明白了。」",
 "6. 戰鬥場景：暗化遮罩 + 玩家騎士 vs 史萊姆 雙肖像、雙方 HP 條與數值、戰鬥日誌「戰鬥！史萊姆」。",
 "7. Boss 戰：第 10 關「巫師王座 (10/10)」，玩家 vs 紅翼惡魔 Vorkath，對話「凡人，也敢染指吾之王座？」「最終決戰。」",
 "8. 成長迴圈：擊敗敵人獲得 EXP 並升級（ATK/DEF/HP 提升），驗證資料→戰鬥→成長閉環。",
 "",
 "所有畫面均以 lavapipe 離屏渲染並存為 PNG 驗證（見下頁快照）。",
]))

# frame pages
def frame_page(fname, caption):
    im=Image.open(os.path.join(ROOT,"frames",fname+".png")).convert("RGB")
    # fit into width
    w,h=im.size; scale=(PW-2*M)/w; tw,th=int(w*scale),int(h*scale)
    im=im.resize((tw,th))
    page=Image.new("RGB",(PW,PH),"white")
    d=ImageDraw.Draw(page)
    d.rectangle([0,0,PW,6],fill=(40,30,70))
    d.text((M,M),caption,font=font(16),fill=(20,20,30))
    page.paste(im,(M,M+30))
    return page

pages.append(frame_page("frame01_stage1","圖 1：第 1 關 — 地城網格、精靈與 HUD"))
pages.append(frame_page("frame02_dialogue_elder","圖 2：長老對話框（繁體中文敘事 + 分支選項）"))
pages.append(frame_page("frame05_combat_start","圖 3：史萊姆戰鬥 — 雙肖像、HP 條、戰鬥日誌"))
pages.append(frame_page("frame08_boss_combat","圖 4：Boss 戰 — Vorkath 紅翼惡魔與最終決戰對話"))

# save multipage
pages[0].save(OUT, "PDF", resolution=96.0, save_all=True, append_images=pages[1:])
print("wrote", OUT, "pages", len(pages))
