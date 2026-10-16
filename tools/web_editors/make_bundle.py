"""make_bundle.py -- copy what the HTML stage editor needs from assets/data into stage_bundle.js.

Run from anywhere:  python tools/web_editors/make_bundle.py
Reads (never writes) assets/data:
    story/floors/F??.json         floor specs (events a floor can roll, nextFloor, handAuthoredStage, ...)
    story/floors/F??.stage.json   the generated grids; boss floors use stages/<handAuthoredStage> instead
    events/pool_*.json            every defined event (id, kind, pool, text key)
    text.json                     only the event strings
    enemies.json, items.json, footprints.json, media/sprites/manifest.json
The page works from this copy; "Open project folder" in the page reads and writes the real files instead.
"""
import datetime
import glob
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
DATA = os.path.join(ROOT, 'assets', 'data')


def load(path):
    """The parsed JSON, plus how the file ends its lines, so a save can write it back byte-compatible."""
    with open(path, 'rb') as f:
        raw = f.read()
    return json.loads(raw.decode('utf8')), {'crlf': b'\r\n' in raw, 'finalNewline': raw.endswith(b'\n')}


def rel(path):
    return os.path.relpath(path, ROOT).replace('\\', '/')


def main():
    floors = []
    for spec_path in sorted((p for p in glob.glob(os.path.join(DATA, 'story', 'floors', '*.json')) if not p.endswith('.stage.json'))):
        spec, spec_eol = load(spec_path)
        hand = spec.get('handAuthoredStage')
        stage_path = os.path.join(DATA, 'stages', hand) if hand else spec_path[:-5] + '.stage.json'
        if not os.path.exists(stage_path):
            continue
        stage, stage_eol = load(stage_path)
        floors.append({'id': spec['id'], 'specPath': rel(spec_path), 'spec': spec, 'specEol': spec_eol,
                       'stagePath': rel(stage_path), 'stage': stage, 'stageEol': stage_eol})

    events = []
    for p in sorted(glob.glob(os.path.join(DATA, 'events', 'pool_*.json'))):
        pool = os.path.basename(p)[:-5]
        for e in load(p)[0]['events']:
            rec = {'id': e['eventId'], 'kind': e['kind'], 'pool': pool, 'text': e['text'], 'title': e.get('title', e['eventId'] + '.title')}
            for k in ('trigger', 'requires', 'actions', 'next',   # event logic, docs/event_editor/06_EVENT_LOGIC.md
                      'dialogue', 'afterDialogue', 'topicOrder', 'once', 'maxTimes'):   # NPC topics, 08_NPC_DIALOGUE.md
                if k in e:
                    rec[k] = e[k]
            events.append(rec)

    strings = load(os.path.join(DATA, 'text.json'))[0]['strings']
    # the NPC files (an NPC's panel: its name, chat lines; docs/event_editor/08_NPC_DIALOGUE.md)
    npcs = {os.path.basename(p)[:-5]: load(p)[0] for p in sorted(glob.glob(os.path.join(DATA, 'npcs', '*.json')))}
    # the palette's type data, whole, for its detail window (docs/19_STAGE_EDITOR.md): written back by Save
    data_files = {}
    for p in [os.path.join(DATA, 'enemies.json'), os.path.join(DATA, 'items.json')] + sorted(glob.glob(os.path.join(DATA, 'npcs', '*.json'))):
        obj, eol = load(p)
        data_files[rel(p)] = {'obj': obj, 'eol': eol}
    # each .anim file's clip names (a type's or an object's animation: "anim/<file>.anim#<clip>")
    anims = {}
    for p in sorted(glob.glob(os.path.join(ROOT, 'assets', 'media', 'anim', '*.anim'))):
        anims['anim/' + os.path.basename(p)] = [c.get('name') for c in load(p)[0].get('clips', []) if c.get('name')]
    # every action a dialogue's choices run: what a talk event can change (the story check's variable reach)
    dialogue_actions = {}
    for p in sorted(glob.glob(os.path.join(DATA, 'dialogue', '*.json'))):
        acts = []
        for node in load(p)[0].get('nodes', {}).values():
            for c in (node.get('choices', []) if isinstance(node, dict) else []):
                a = c.get('actions')
                acts += a if isinstance(a, list) else [a] if isinstance(a, dict) else []
        if acts:
            dialogue_actions[os.path.basename(p)[:-5]] = acts
    used = {e['text'] for e in events} | {e['title'] for e in events} | {i for f in floors for i in f['spec'].get('events', [])}
    used |= {n.get(k) for n in npcs.values() for k in ('name', 'greeting') if n.get(k)}
    used |= {v.get(k) for kk, v in load(os.path.join(DATA, 'items.json'))[0].items() if not kk.startswith('_')
             for k in ('name', 'desc') if isinstance(v.get(k), str)}
    texts = {k: {l: strings[k].get(l) for l in ('zh_TW', 'en')} for k in sorted(used) if k in strings}

    pick = lambda d: {l: d.get(l) for l in ('zh_TW', 'en')} if isinstance(d, dict) else d
    enemies = {k: {'name': pick(v.get('name')), 'hp': v.get('hp'), 'atk': v.get('atk'), 'def': v.get('def')}
               for k, v in load(os.path.join(DATA, 'enemies.json'))[0].items() if not k.startswith('_')}
    # items.json is the all-item file: name/desc are text.json keys, the bonuses are "stats", and it also holds
    # weapons / armor / talents ("type"), which the stage editor does not place on floors.
    textOf = lambda n: pick(strings.get(n, n)) if isinstance(n, str) else pick(n)
    # 'tint': a story item that opens a gate ("gate:<item id>") colours the gate's door sprite
    items = {k: {'name': textOf(v.get('name')), 'effect': v.get('stats', v.get('effect')), 'type': v.get('type', 'consumable'),
                 **({'tint': v['tint']} if 'tint' in v else {})}
             for k, v in load(os.path.join(DATA, 'items.json'))[0].items() if not k.startswith('_')}
    footprints = {k: v for k, v in load(os.path.join(DATA, 'footprints.json'))[0].items() if not k.startswith('_')}
    sprites = load(os.path.join(ROOT, 'assets', 'media', 'sprites', 'manifest.json'))[0]['sprites']
    kp = os.path.join(DATA, 'events', 'kinds.json')
    kinds = {k: v for k, v in load(kp)[0].items() if not k.startswith('_')} if os.path.exists(kp) else {}

    stamp = datetime.date.today().isoformat()
    bundle = {'snapshot': stamp, 'floors': floors, 'events': events, 'texts': texts,
              'enemies': enemies, 'items': items, 'footprints': footprints, 'sprites': sprites, 'kinds': kinds, 'npcs': npcs,
              'dialogueActions': dialogue_actions, 'dataFiles': data_files, 'anims': anims}
    out = os.path.join(HERE, 'stage_bundle.js')
    with open(out, 'w', encoding='utf8') as f:
        f.write(f'// Generated by make_bundle.py from assets/data on {stamp}. A read-only copy for stage_editor.html.\n')
        f.write('const BUNDLE = ' + json.dumps(bundle, ensure_ascii=False) + ';\n')
    print(f'{len(floors)} floors, {len(events)} events, {len(enemies)} enemies, {len(items)} items -> {rel(out)}')


if __name__ == '__main__':
    main()
