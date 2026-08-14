#!/usr/bin/env python3
"""Generate game content for Tower of the Sorcerer (Vulkan C++).
Produces data/ with:
  story.json        - narrative bible (why defeat the boss)
  enemies.json      - enemy stat templates
  items.json        - item/powerup definitions
  combat.json       - combat (round-by-round auto punch) parameters
  dialogue/*.json   - talking-system trees for NPCs and enemies
  stages/stageNN.json - 10 stages: layout + enemies + items + connections
All coordinates are grid cells. Layouts are char maps (see LEGEND).
"""
import os, json

BASE = "/home/fatming/tower_vulkan/data"
os.makedirs(os.path.join(BASE, "stages"), exist_ok=True)
os.makedirs(os.path.join(BASE, "dialogue"), exist_ok=True)

# ---------------------------------------------------------------------------
# STORY
# ---------------------------------------------------------------------------
story = {
    "title": "Tower of the Sorcerer",
    "subtitle": "魔法塔 — 巫師之塔",
    "premise": (
        "十年前，黑巫師之王 Vorkath（沃卡司）以禁咒封印了『安穩之星』——王國生命的源泉，"
        "並將唯一的反咒之匙交給了被囚於塔頂的公主 Liora（莉歐拉）。從此大地枯萎、怪物橫行。"
        "你是年輕的見習巫師，必須攀上十層『巫師之塔』，擊敗 Vorkath，救出公主、重啟安穩之星。"
    ),
    "final_boss": {
        "id": "demonlord_vorkath", "name": "Vorkath 沃卡司", "floor": 10,
        "why": "他是封印的源頭；不擊敗他，安穩之星永不重啟，國土將持續凋零。"
    },
    "arc": [
        "序章：村莊外緣 — 長老告知災禍起源。",
        "習戰：森林小徑 — 老巫師教你『自動回合拳戰』。",
        "奪鑰：門樓 — 擊敗石巨人以取得第一把鑰匙。",
        "求知：藏書閣 — 骷髏學者揭示 Vorkath 的弱點。",
        "亡者之語：墓穴 — 村民亡魂證言公主仍活著。",
        "試煉：洞窟 — 取得攻防寶石強化自身。",
        "王者之援：軍營 — 王之副官贈予祝福。",
        "聖所：神殿 — 公主侍女透露頂層機關。",
        "終焉前廳：王座前室 — 公主親口託付希望。",
        "決戰：巫師王座 — 與 Vorkath 一戰。",
    ],
}

# ---------------------------------------------------------------------------
# ENEMIES (stat templates; combat uses these)
# ---------------------------------------------------------------------------
enemies = {
    "slime":   {"name": "史萊姆",   "sprite": "slime.png",   "hp": 24,  "atk": 8,  "def": 2,  "exp": 6,  "gold": 4},
    "bat":     {"name": "暗影蝙蝠", "sprite": "bat.png",     "hp": 18,  "atk": 11, "def": 1,  "exp": 7,  "gold": 5},
    "golem":   {"name": "石巨人",   "sprite": "golem.png",   "hp": 70,  "atk": 14, "def": 8,  "exp": 20, "gold": 12},
    "skeleton":{"name": "骷髏兵",   "sprite": "skeleton.png","hp": 40,  "atk": 16, "def": 4,  "exp": 14, "gold": 9},
    "wraith":  {"name": "怨靈",     "sprite": "wraith.png",  "hp": 55,  "atk": 20, "def": 3,  "exp": 22, "gold": 14},
    "demon":   {"name": "小惡魔",   "sprite": "demon.png",   "hp": 90,  "atk": 24, "def": 9,  "exp": 40, "gold": 22},
    "demonlord_vorkath": {"name": "Vorkath 沃卡司", "sprite": "boss_demonlord.png", "hp": 400, "atk": 34, "def": 14, "exp": 0, "gold": 0, "boss": True},
}

# ---------------------------------------------------------------------------
# ITEMS / POWERUPS
# ---------------------------------------------------------------------------
items = {
    "gem_atk":  {"name": "攻擊寶石", "sprite": "gem_atk.png",  "effect": {"atk": 6}},
    "gem_def":  {"name": "防禦寶石", "sprite": "gem_def.png",  "effect": {"def": 5}},
    "potion_red": {"name": "紅血瓶", "sprite": "potion_red.png", "effect": {"hp": 40}},
    "potion_blue":{"name": "藍血瓶", "sprite": "potion_blue.png","effect": {"hp": 80}},
    "coin":     {"name": "金幣",     "sprite": "coin.png",     "effect": {"gold": 10}},
    "key_yellow": {"name": "黃鑰匙", "sprite": "key_yellow.png","effect": {"key_yellow": 1}},
    "key_blue":  {"name": "藍鑰匙", "sprite": "key_blue.png",  "effect": {"key_blue": 1}},
    "key_red":   {"name": "紅鑰匙", "sprite": "key_red.png",   "effect": {"key_red": 1}},
}

# ---------------------------------------------------------------------------
# COMBAT (auto round-by-round punching)
# ---------------------------------------------------------------------------
combat = {
    "model": "auto_round",
    "description": "進入戰鬥後雙方自動輪流出拳，直到一方 HP 歸零。",
    "round_time_ms": 700,
    "formula": {
        "dmg_to_target": "max(1, attacker.atk - target.def)",
        "hits_to_kill": "ceil(target.hp / dmg_to_target)",
        "damage_taken": "(hits_to_kill - 1) * max(1, target.atk - attacker.def)"
    },
    "player_base": {"hp": 120, "atk": 12, "def": 4},
    "win_reward": "擊敗敵人獲得 exp 與 gold；exp 達門檻自動升級 +atk/+def。",
    "lose": "玩家 HP 歸零 → 回到本層起點（保留屬性）。"
}

# ---------------------------------------------------------------------------
# LEGEND for stage char maps
# ---------------------------------------------------------------------------
LEGEND = {
    "#": "wall", ".": "floor", "@": "player_start",
    "U": "stairs_up", "D": "stairs_down",
    "y": "door:yellow", "b": "door:blue", "r": "door:red",
    "Y": "key:yellow", "B": "key:blue", "R": "key:red",
    "a": "item:gem_atk", "d": "item:gem_def",
    "h": "item:potion_red", "H": "item:potion_blue", "c": "item:coin",
    "s": "npc:sorcerer", "v": "npc:villager", "k": "npc:king",
    "p": "npc:princess", "m": "npc:handmaiden",
    "1": "monster:slime", "2": "monster:bat", "3": "monster:golem",
    "4": "monster:skeleton", "5": "monster:wraith", "6": "monster:demon",
    "Z": "monster:demonlord_vorkath",
}

# Helper to assemble a stage file
W, H = 13, 11
def stage(idx, name, subtitle, up=None, down=None, npcs=None, monsters=None,
          items_=None, keys_doors=None, w=13, h=11, story_note=""):
    if npcs is None: npcs = []
    if monsters is None: monsters = []
    if items_ is None: items_ = []
    if keys_doors is None: keys_doors = []
    # build a simple border room with interior; place entities by dict of (x,y)->char
    grid = [["#" for _ in range(w)] for _ in range(h)]
    for y in range(1, h-1):
        for x in range(1, w-1):
            grid[y][x] = "."
    place = {}
    place[(1, H-2)] = "@"           # player start bottom-left
    if up:   place[(W//2, 1)] = "U"
    if down: place[(W//2, 1)] = "D"
    for (x, y, ch) in npcs + monsters + items_ + keys_doors:
        place[(x, y)] = ch
    for (x, y), ch in place.items():
        grid[y][x] = ch
    tiles = ["".join(r) for r in grid]
    return {
        "id": f"stage_{idx:02d}", "name": name, "subtitle": subtitle,
        "index": idx, "width": w, "height": h,
        "legend": LEGEND, "tiles": tiles,
        "story_note": story_note,
        "connect": {"up": up, "down": down},
    }

stages = []

# 1 Village Outskirts (tutorial)
stages.append(stage(1, "村莊外緣", "Village Outskirts",
    up="stage_02",
    npcs=[(2, H-2, "v")],
    monsters=[(6, 5, "1"), (8, 7, "1"), (10, 4, "2")],
    items_=[(11, 9, "h")],
    story_note="長老：『沃卡司封印了安穩之星，公主被囚塔頂。去吧，攀上巫師之塔。』"))

# 2 Forest Path (learn combat)
stages.append(stage(2, "森林小徑", "Forest Path",
    up="stage_03", down="stage_01",
    npcs=[(2, H-2, "s")],
    monsters=[(5, 3, "1"), (7, 6, "2"), (9, 4, "1"), (11, 8, "2")],
    items_=[(10, 9, "a"), (3, 3, "c")],
    story_note="老巫師：『遇敵即自動輪流出拳，看準攻防再上。』"))

# 3 Gatehouse (get first key + golem)
stages.append(stage(3, "門樓", "Gatehouse",
    up="stage_04", down="stage_02",
    monsters=[(6, 5, "3"), (9, 7, "1")],
    keys_doors=[(11, 5, "y"), (10, 5, "Y")],
    items_=[(2, 3, "h")],
    story_note="石巨人守著黃門；擊敗它取得黃鑰匙，門後是藏書閣。"))

# 4 Library (lore skeleton)
stages.append(stage(4, "藏書閣", "Library",
    up="stage_05", down="stage_03",
    npcs=[(2, H-2, "k")],
    monsters=[(5, 4, "4"), (8, 6, "4"), (10, 3, "1")],
    items_=[(11, 9, "d"), (3, 3, "c"), (9, 9, "H")],
    story_note="骷髏學者：『沃卡司懼怕純淨之攻與堅毅之防——疊滿寶石方可撼動他。』"))

# 5 Crypt (ghost villager)
stages.append(stage(5, "墓穴", "Crypt",
    up="stage_06", down="stage_04",
    npcs=[(2, H-2, "v")],
    monsters=[(5, 3, "4"), (7, 6, "5"), (9, 4, "4"), (11, 8, "5")],
    items_=[(10, 9, "a"), (3, 3, "h")],
    story_note="村民亡魂：『別怕……公主還活著，她在等你去。』"))

# 6 Cavern (power-up gems)
stages.append(stage(6, "洞窟", "Cavern",
    up="stage_07", down="stage_05",
    monsters=[(5, 4, "3"), (8, 7, "5"), (10, 3, "6")],
    keys_doors=[(11, 5, "b"), (10, 5, "B")],
    items_=[(2, 3, "a"), (3, 3, "d"), (9, 9, "H"), (6, 9, "c")],
    story_note="洞窟深處藏著攻防雙寶石；藍門後是王之軍營。"))

# 7 Barracks (king's lieutenant boon)
stages.append(stage(7, "軍營", "Barracks",
    up="stage_08", down="stage_06",
    npcs=[(2, H-2, "k")],
    monsters=[(5, 3, "4"), (7, 6, "6"), (9, 4, "5"), (11, 8, "6")],
    items_=[(10, 9, "a"), (3, 3, "d"), (9, 9, "H")],
    story_note="王之副官：『這枚祝福給你——但真正的力量，來自你一路的成長。』"))

# 8 Sanctum (handmaiden clue)
stages.append(stage(8, "神殿", "Sanctum",
    up="stage_09", down="stage_07",
    npcs=[(2, H-2, "m")],
    monsters=[(5, 4, "5"), (8, 7, "6"), (10, 3, "6")],
    items_=[(11, 9, "a"), (3, 3, "H"), (6, 9, "d")],
    story_note="侍女：『頂層有禁咒機關，唯有擊碎沃卡司本體才能解除。』"))

# 9 Throne Antechamber (princess speaks)
stages.append(stage(9, "王座前室", "Throne Antechamber",
    up="stage_10", down="stage_08",
    npcs=[(2, H-2, "p")],
    monsters=[(5, 3, "6"), (7, 6, "6"), (9, 4, "5"), (11, 8, "6")],
    items_=[(10, 9, "H"), (3, 3, "a"), (6, 9, "d")],
    story_note="公主 Liora：『我信你。打碎他，安穩之星便會重燃。』"))

# 10 Demon Lord's Throne (boss)
stages.append(stage(10, "巫師王座", "Demon Lord's Throne",
    down="stage_09",
    monsters=[(W//2, 3, "Z")],
    items_=[(2, H-2, "H")],
    story_note="Vorkath：『凡人，也敢踏足吾之王座？』——最終決戰。"))

# ---------------------------------------------------------------------------
# DIALOGUE (talking system) - one file per NPC/enemy id
# ---------------------------------------------------------------------------
def dlg(npc_id, start, nodes):
    return {"npcId": npc_id, "start": start, "nodes": nodes}

dialogues = {
    "villager_elder": dlg("villager_elder", "root", {
        "root": {"text": "年輕人，災禍起源於十年前。黑巫師 Vorkath 封印了安穩之星，並將公主囚於塔頂。",
                "choices": [{"label": "我該怎麼做？", "next": "how"}, {"label": "明白了。", "next": None}]},
        "how": {"text": "攀上這十層巫師之塔，變強，最後擊敗 Vorkath。路上寶石能強化你。",
                "choices": [{"label": "我會做到。", "next": None}]},
    }),
    "sorcerer_teacher": dlg("sorcerer_teacher", "root", {
        "root": {"text": "戰鬥是自動的——你走向敵人便會輪流出拳，直到一方倒下。",
                "choices": [{"label": "那我該注意什麼？", "next": "tip"}, {"label": "懂了。", "next": None}]},
        "tip": {"text": "先吃攻擊寶石再打高防敵人，損血最少。防禦則降低你承受的每一擊。",
                "choices": [{"label": "記住了。", "next": None}]},
    }),
    "king_lieutenant": dlg("king_lieutenant", "root", {
        "root": {"text": "王國將希望寄託於你。這份祝福，願它護你周全。",
                "action": {"give": "potion_blue", "amount": 1},
                "choices": [{"label": "感謝陛下。", "next": None}]},
    }),
    "skeleton_scholar": dlg("skeleton_scholar", "root", {
        "root": {"text": "吾曾為宮廷學者。Vorkath 之弱，在於純淨之攻與堅毅之防。",
                "choices": [{"label": "意思是？", "next": "weak"}, {"label": "受教了。", "next": None}]},
        "weak": {"text": "把攻防寶石疊滿，他的高防便如紙糊。切記。",
                "choices": [{"label": "我會變強。", "next": None}]},
    }),
    "ghost_villager": dlg("ghost_villager", "root", {
        "root": {"text": "別怕……公主還活著。她在塔頂，等著被拯救。",
                "choices": [{"label": "我會帶她回來。", "next": None}]},
    }),
    "handmaiden": dlg("handmaiden", "root", {
        "root": {"text": "頂層有禁咒機關，唯有擊碎沃卡司本體，封印才會解除。",
                "choices": [{"label": "我明白了。", "next": None}]},
    }),
    "princess_liora": dlg("princess_liora", "root", {
        "root": {"text": "你來了……我信你。打碎他，安穩之星便會重燃。",
                "choices": [{"label": "等我。", "next": None}]},
    }),
    # Enemy pre-battle barks
    "enemy_slime": dlg("enemy_slime", "root", {
        "root": {"text": "咕嚕……（史萊姆擋住了去路）", "choices": [{"label": "（戰鬥）", "next": None}]}}),
    "enemy_bat": dlg("enemy_bat", "root", {
        "root": {"text": "嘶——（蝙蝠撲翅而來）", "choices": [{"label": "（戰鬥）", "next": None}]}}),
    "enemy_golem": dlg("enemy_golem", "root", {
        "root": {"text": "石巨人：『此地不容活物。』", "choices": [{"label": "（戰鬥）", "next": None}]}}),
    "enemy_skeleton": dlg("enemy_skeleton", "root", {
        "root": {"text": "骷髏：『加入我們的行列吧……』", "choices": [{"label": "（戰鬥）", "next": None}]}}),
    "enemy_wraith": dlg("enemy_wraith", "root", {
        "root": {"text": "怨靈：『仇恨……永不消散……』", "choices": [{"label": "（戰鬥）", "next": None}]}}),
    "enemy_demon": dlg("enemy_demon", "root", {
        "root": {"text": "小惡魔：『主人在等你獻祭！』", "choices": [{"label": "（戰鬥）", "next": None}]}}),
    "enemy_demonlord": dlg("enemy_demonlord", "root", {
        "root": {"text": "Vorkath：『凡人，也敢踏足吾之王座？安穩之星將永世沉睡！』",
                "choices": [{"label": "（決戰）", "next": None}]}}),
}

# ---------------------------------------------------------------------------
# WRITE FILES
# ---------------------------------------------------------------------------
def dump(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)

dump(os.path.join(BASE, "story.json"), story)
dump(os.path.join(BASE, "enemies.json"), enemies)
dump(os.path.join(BASE, "items.json"), items)
dump(os.path.join(BASE, "combat.json"), combat)
for i, s in enumerate(stages, 1):
    dump(os.path.join(BASE, "stages", f"stage{i:02d}.json"), s)
for did, d in dialogues.items():
    dump(os.path.join(BASE, "dialogue", f"{did}.json"), d)

print("Wrote content:")
print("  stages:", len(stages))
print("  dialogues:", len(dialogues))
print("  enemies:", len(enemies), "items:", len(items))
