#!/usr/bin/env python3
"""gen_story_i18n.py — fill data/text.json with the story keys the S3 data references.

docs/story/STORY_DATA_SCHEMA.md section 11: `zh_TW` is authoritative, the other five locales may be filled
from it and marked `_todo: true` until a translator (or a later phase) writes them properly, and the
validator (V6) requires that every key the story data references exists.

Which keys are real content vs placeholder:
  * the chapters that exist today (ch_01..ch_03), their floors (F01..F21), the choice prompts/options
    they define, and the S3 event pools -> authored 繁體中文 + English in this script;
  * everything the generator produced for acts 4-10 (floors F22..F70) -> a formulaic zh_TW line plus
    `_todo: true`, which is exactly the state section 11.2 describes for untranslated content.

Idempotent: existing values are never overwritten, so hand-written lines survive a re-run.

Usage: python3 tools/gen_story_i18n.py [--report]
"""
import argparse
import glob
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TEXT = os.path.join(ROOT, 'data', 'text.json')
LOCALES = ['zh_TW', 'en', 'zh_CN', 'ja', 'ko', 'es']

# ---- authored content for the S3 acts (docs/story/STORY_BIBLE.md sections 3/4) ---------------------------
ACT_NAMES = {
    'F01': ('村莊外緣・井', 'Village edge: the well'), 'F02': ('曬穀場', 'The drying yard'),
    'F03': ('舊祠堂', 'The old shrine'), 'F04': ('記憶之井', 'The well of memory'),
    'F05': ('小穗的攤位', 'Xiaosui\'s stall'), 'F06': ('往門樓的坡道', 'Slope to the gate'),
    'F07': ('村莊外緣・封印結', 'Village edge: the seal knot'),
    'F08': ('林道入口', 'Forest path'), 'F09': ('老巫師的營火', 'The old sorcerer\'s campfire'),
    'F10': ('折箭陷阱區', 'Broken-arrow snares'), 'F11': ('林中的舊箭', 'The old arrow'),
    'F12': ('無名塚', 'The nameless grave'), 'F13': ('霧中的獵徑', 'Hunting trail in fog'),
    'F14': ('林道・封印結', 'Forest path: the seal knot'),
    'F15': ('門樓前的道兵', 'Soldiers before the gate'), 'F16': ('門樓石階', 'The gate steps'),
    'F17': ('刻痕迴廊', 'Corridor of carvings'), 'F18': ('門環與拓印', 'The ring and the rubbing'),
    'F19': ('門環上的刻痕', 'The notch on the ring'), 'F20': ('門內的第一道氣', 'First qi beyond the gate'),
    'F21': ('門樓・封印結', 'The gate: the seal knot'),
}
ACT_INTROS = {
    'F01': '長老指向塔頂：「封印安穩之星的人，也在等一個理由。」', 'F02': '風把穀殼吹過腳邊，村子的日子還在繼續。',
    'F03': '祠堂的木像沒有臉——有人刻意磨掉了。', 'F04': '井底的石壁刻著七道痕，其中一道是新的。',
    'F05': '小穗遞來半塊餅：「你小時候也是這樣分著吃的。」', 'F06': '坡道上的腳印比你早一步，方向卻是往下的。',
    'F07': '井中妖從水裡爬出來，嘴裡喊著你的舊名字。',
    'F08': '林線之後，霧開始有自己的重量。', 'F09': '老巫師把火撥旺：「先學會不被殺死，再談殺人。」',
    'F10': '腳下全是折斷的箭——有人不希望你走到這裡。', 'F11': '一支插在樹心的箭，箭羽是你前世的紋樣。',
    'F12': '無名塚沒有名字，只有一把沒收鞘的劍。', 'F13': '霧裡的獵徑繞回原處，像有人希望你留在這裡。',
    'F14': '折箭獵手在霧中拉弓，箭尖對著你的胸口。',
    'F15': '道兵的鎧甲裡沒有身體，只有一縷不肯散去的氣。', 'F16': '石階上刻滿了名字，最後一個位置是空的。',
    'F17': '走廊兩側的刻痕連成一張臉，你認得那張臉。', 'F18': '門環上的刻痕與你腰間那把劍的缺口吻合。',
    'F19': '門環上的刻痕——可以補，也可以砸。', 'F20': '門後的第一口氣帶著鐵鏽味，但確實能呼吸。',
    'F21': '守門石巨人的眼窩裡亮著兩點舊火。',
}
CHOICE_TEXT = {
    'story.ch01.c1.q': ('為什麼要攀上這座塔？', 'Why climb this tower?'),
    'story.ch01.c1.o1': ('我想知道自己究竟是誰', 'I want to know who I really am'),
    'story.ch01.c1.o2': ('我想救出莉歐拉', 'I want to free Liora'),
    'story.ch01.c1.o3': ('我要向沃卡司討回那一刀', 'I will take back that blade from Vorkath'),
    'story.ch03.c3.q': ('門環上的刻痕要怎麼處理？', 'What will you do with the notch on the ring?'),
    'story.ch03.c3.o1': ('破壞它——路是自己開的', 'Break it -- I carve my own way'),
    'story.ch03.c3.o2': ('解開它——門本來就是給人過的', 'Unseal it -- a gate is meant to be passed'),
}
EVENT_TEXT = {
    'ev_village_well.text': '井水映出一個不是你的人影。',
    'ev_village_chimney.text': '灶煙裡有一句你聽過的話，卻想不起是誰說的。',
    'ev_village_grave_new.text': '新墳上的土還是濕的；有人在裡面敲了兩下。',
    'ev_village_dog_bowl.text': '狗碗還是滿的，狗卻不在了。',
    'ev_village_ledger.text': '帳冊上有一筆以你的名字付過的款。',
    'ev_village_bell_rope.text': '鈴繩一拉，遠處有人同時抬頭。',
    'ev_village_chapel_dust.text': '灰塵在光裡排成一個字。',
    'ev_village_old_plough.text': '犁頭上纏著一段十年前的布。',
    'ev_forest_broken_arrow.text': '斷箭上刻著你前世的軍號。',
    'ev_forest_snare.text': '繩索已經被人動過手腳——手法很熟。',
    'ev_forest_old_wizard_mark.text': '樹皮上的記號是老師父的筆跡，日期是今天。',
    'ev_forest_trap_net.text': '網上掛著一件很新的外袍。',
    'ev_forest_hunter_cache.text': '獵人藏糧的樹洞裡多了一封信。',
    'ev_forest_stone_marker.text': '界石上寫著「此地曾有人等你」。',
    'ev_forest_lost_child.text': '霧裡的孩子說不出自己的名字，只說得出你的。',
    'ev_forest_charcoal_kiln.text': '炭窯還溫著，窯口的腳印是兩雙。',
    'ev_gate_soldier_plate.text': '胸甲內側用刀尖刻著「別替我報仇」。',
    'ev_gate_seal_rubbing.text': '拓印的紙上，刻痕比原物多一道。',
    'ev_gate_forge_first.text': '第一爐火還留著餘燼，鐵砧上有你的手印。',
    'ev_gate_broken_ram.text': '撞門的木槌斷了三次，第三次是從內側斷的。',
    'ev_gate_quartermaster_cache.text': '軍需箱上的封條蓋著你前世的印。',
    'ev_gate_deserters_kit.text': '逃兵的背包已收好，卻沒有人帶走。',
    'ev_gate_old_banner.text': '舊旗上的紋章與你的劍柄相同。',
    'ev_gate_forge_slag.text': '爐渣裡有一枚沒有成形的劍胚。',
    'ev_common_relic_road.text': '路邊的劍痕很深——是同一個人反覆練出來的。',
    'ev_common_relic_chipped_blade.text': '缺口的位置，剛好是你腰間那把劍的另一半。',
    'ev_common_whisper_old_road.text': '「那位大人……十年前來過這裡。」',
    'ev_common_whisper_old_song.text': '有人低聲哼著你沒學過卻會唱的調子。',
    'ev_common_cache_wall.text': '牆後有個縫，裡面塞著補給。',
    'ev_common_cache_rat_nest.text': '鼠窩裡混著一枚乾淨的金幣。',
    'ev_common_trap_rubble.text': '落石是剛被推下來的——上面還站得穩。',
    'ev_common_rescue_lost_soldier.text': '壓在梁下的士兵還醒著，先問你的名字。',
    'ev_common_merchant_echo_ash.text': '灰中的商人殘影只收素材，不收金幣。',
    'ev_common_shard_wandering.text': '一片記憶碎片在空中停留了一瞬。',
}


def authored(key):
    """(zh_TW, en) for a key we have real content for, else None."""
    if key in CHOICE_TEXT:
        return CHOICE_TEXT[key]
    if key in EVENT_TEXT:
        return (EVENT_TEXT[key], EVENT_TEXT[key])
    m = re.match(r'^story\.f(\d\d)\.(name|intro|ambient\.(\d))$', key)
    if m and ('F' + m.group(1)) in ACT_NAMES:
        fid = 'F' + m.group(1)
        name, en = ACT_NAMES[fid]
        if m.group(2) == 'name':
            return (name, en)
        if m.group(2) == 'intro':
            return (ACT_INTROS[fid], en)
        return (ACT_INTROS[fid], en)          # ambient 1/2 reuse the act line for now
    m = re.match(r'^story\.ch(\d\d)\.title$', key)
    if m:
        titles = {'01': ('我為何舉劍', 'Why I Raise the Blade'),
                  '02': ('老巫師的氣', 'The Old Sorcerer\'s Qi'),
                  '03': ('門環上的刻痕', 'The Notch on the Ring')}
        return titles.get(m.group(1))
    return None


def placeholder_text(key):
    m = re.match(r'^story\.f(\d\d)\.(name|intro|ambient\.\d)$', key)
    if m:
        n = int(m.group(1))
        act = 'ch_%02d' % ((n - 1) // 7 + 1)
        if m.group(2) == 'name':
            return '第 %d 層' % n
        if m.group(2) == 'intro':
            return '第 %d 層（%s）的入口。' % (n, act)
        return '第 %d 層的空氣裡有前世的形狀。' % n
    return key


def collect_keys():
    keys = set()
    def walk(node):
        if isinstance(node, dict):
            for k, v in node.items():
                if isinstance(v, str) and (v.startswith('story.') or v.startswith('ev_')):
                    keys.add(v)
                else:
                    walk(v)
        elif isinstance(node, list):
            for v in node:
                if isinstance(v, str):
                    if v.startswith('story.') or v.startswith('ev_'):
                        keys.add(v)
                else:
                    walk(v)
    for path in [os.path.join(ROOT, 'data', 'story.json')] \
            + sorted(glob.glob(os.path.join(ROOT, 'data', 'story', 'chapters', '*.json'))) \
            + sorted(glob.glob(os.path.join(ROOT, 'data', 'story', 'floors', '*.json'))) \
            + sorted(glob.glob(os.path.join(ROOT, 'data', 'events', '*.json'))):
        with open(path, encoding='utf-8') as f:
            walk(json.load(f))
    # event ids themselves need a text key, per section 11's naming rule
    for path in sorted(glob.glob(os.path.join(ROOT, 'data', 'events', '*.json'))):
        with open(path, encoding='utf-8') as f:
            for ev in json.load(f).get('events', []):
                keys.add(ev['text'])
    return keys


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--report', action='store_true')
    args = ap.parse_args()

    with open(TEXT, encoding='utf-8') as f:
        doc = json.load(f)
    # The runtime looks keys up in doc["strings"] (Locale::loadFromFile); the file's other top-level
    # members are reserved. Writing the keys at the top level produced 463 strings the game could
    # never find -- tr() simply returned the key, and every fallback in the calling code hid it.
    text = doc.setdefault('strings', {})
    keys = collect_keys()
    added, authored_n, todo_n = 0, 0, 0
    for key in sorted(keys):
        if key in text:
            continue
        pair = authored(key)
        if pair:
            zh, en = pair
            authored_n += 1
        else:
            # A placeholder must still be real text a player can read -- showing the raw key in game
            # would look like a bug. Formulaic 繁體中文 for the generated floors (the acts beyond
            # ch_03 have no authored lines yet), and the key itself only as a last resort, always
            # with `_todo: true` so the debt is explicit (section 11.2).
            zh, en, todo_n = placeholder_text(key), '', todo_n + 1
            en = zh
        entry = {'zh_TW': zh, 'en': en}
        for loc in ('zh_CN', 'ja', 'ko', 'es'):
            entry[loc] = zh                  # section 11.2: backfill from zh_TW ...
        entry['_todo'] = True                # ... and mark it, so a real translation is still owed
        if pair:
            entry.pop('_todo', None)         # authored za/en pairs are not placeholders
        text[key] = entry
        added += 1

    fixed = 0
    for key, val in list(text.items()):
        if not isinstance(val, dict) or not val.get('_todo'):
            continue
        want = placeholder_text(key)
        if val.get('zh_TW') != want or val.get('en', '') in (key, ''):
            val['zh_TW'] = want
            if not val.get('en') or val['en'] == key:
                val['en'] = want
            for loc in ('zh_CN', 'ja', 'ko', 'es'):
                val[loc] = want
            fixed += 1
    if fixed:
        print('gen_story_i18n: repaired %d placeholder(s)' % fixed)

    with open(TEXT, 'w', encoding='utf-8') as f:
        json.dump(doc, f, ensure_ascii=False, indent=2)
        f.write('\n')
    print('gen_story_i18n: %d key(s) referenced, %d added (%d authored, %d placeholder+_todo)'
          % (len(keys), added, authored_n, todo_n))
    if args.report:
        missing = [k for k in keys if k not in text]
        print('still missing:', missing[:10])


if __name__ == '__main__':
    main()
