"""Give every object whose state the game saves a stable id in its stage file (docs/28_SAVE_SYSTEM.md).

    python tools/add_stage_uids.py [--check]

Monsters, items, keys, events, gates and doors get objects."x,y".uid ("o1", "o2" ... unique on the floor), the same
rule as the stage editor's Project::ensureUids, which keeps them up to date from then on. The save keys an object's
status (defeated, collected, opened) by this id, so moving the object in the editor does not bring a defeated monster
back. Files are rewritten in their own layout (2-space JSON, their own line endings); a file that needs nothing is not
touched. --check only reports the files that lack ids (exit 1 if any).
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = sorted(glob.glob(os.path.join(ROOT, "assets/data/story/floors/*.stage.json")) +
               glob.glob(os.path.join(ROOT, "assets/data/stages/*.json")))
SAVES_STATE = ("monster:", "item:", "key:", "event:", "gate:", "door:")


def add_uids(st):
    legend = st.get("legend", {})
    objs = st.get("objects") if isinstance(st.get("objects"), dict) else {}
    nums = [int(m.group(1)) for o in objs.values() if isinstance(o, dict)
            for m in [re.fullmatch(r"o(\d+)", str(o.get("uid", "")))] if m]
    nxt = max(nums, default=0) + 1
    used, added = set(), 0
    for y, row in enumerate(st.get("tiles", [])):
        for x, ch in enumerate(row):
            sem = legend.get(ch, "")
            if not sem.startswith(SAVES_STATE):
                continue
            key = f"{x},{y}"
            o = objs.get(key) if isinstance(objs.get(key), dict) else None
            uid = o.get("uid") if o else None
            if isinstance(uid, str) and uid and uid not in used:
                used.add(uid)
                continue
            uid = f"o{nxt}"
            nxt += 1
            used.add(uid)
            objs.setdefault(key, {})["uid"] = uid
            added += 1
    if added:
        st["objects"] = objs
    return added


def main():
    check = "--check" in sys.argv
    missing = 0
    for f in FILES:
        raw = open(f, encoding="utf-8", newline="").read()
        st = json.loads(raw)
        n = add_uids(st)
        if not n:
            continue
        missing += 1
        rel = os.path.relpath(f, ROOT)
        if check:
            print(f"{rel}: {n} object(s) without a uid")
            continue
        text = json.dumps(st, indent=2, ensure_ascii=False) + ("\n" if raw.endswith("\n") else "")
        if "\r\n" in raw:
            text = text.replace("\n", "\r\n")
        open(f, "w", encoding="utf-8", newline="").write(text)
        print(f"{rel}: {n} uid(s) added")
    if check and missing:
        sys.exit(1)
    print(f"{len(FILES)} stage files, {missing} {'lack ids' if check else 'updated'}")


if __name__ == "__main__":
    main()
