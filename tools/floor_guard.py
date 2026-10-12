#!/usr/bin/env python3
"""floor_guard.py -- keeps the floor generator from silently replacing floors a person edited (Q6).

The generator (tools/gen_floors.py, then tools/gen_stairs.py) never writes into assets/data directly.
It writes into a staging copy; this module then compares the staging copy with the real files:

  * a real file that matches what the generator wrote last time (its hash in
    tools/floor_gen_manifest.json) is the generator's own: it is replaced without asking;
  * a real file that does NOT match (edited in the stage editor, by hand, or never recorded) and that
    the new output would change is a person's work: the floors are listed in a dialog --
    "Floors 4, 5, 6, 55 were changed by hand. The generator will place its own data there." --
    and nothing is written until the answer:
        Yes    -- replace them too
        No     -- keep the hand edits: those floors are skipped, the rest is written
        Cancel -- write nothing.
    Without a display (or with TOMS_NO_POPUPS=1) the same question is asked on the console; with no
    console either, the answer is Cancel. --yes / --keep-edits answer it up front.

Files are compared as JSON (formatting and line endings do not count), and a replaced file keeps its
own line endings (the boss stages use CRLF).
"""
import hashlib
import json
import os
import re
import shutil
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, 'assets', 'data')
FLOORS_DIR = os.path.join(DATA, 'story', 'floors')
STAGES_DIR = os.path.join(DATA, 'stages')
MANIFEST = os.path.join(ROOT, 'tools', 'floor_gen_manifest.json')

# The hand-authored boss stage of each act's 7th floor (docs/story: STORY_DATA_SCHEMA 3.2).
BOSS_STAGE = {
    7: "stage01.json", 14: "stage02.json", 21: "stage03.json", 28: "stage04.json",
    35: "stage05.json", 42: "stage06.json", 49: "stage07.json", 56: "stage08.json",
    63: "stage09.json", 70: "stage10.json",
}
_FLOOR_OF_STAGE = {v: k for k, v in BOSS_STAGE.items()}


def _rel(path, base):
    return os.path.relpath(path, base).replace('\\', '/')


def floor_of(rel):
    """'story/floors/F04.stage.json' -> 4, 'stages/stage01.json' -> 7, else None."""
    m = re.match(r'story/floors/F(\d+)(\.stage)?\.json$', rel)
    if m:
        return int(m.group(1))
    return _FLOOR_OF_STAGE.get(rel.split('/')[-1]) if rel.startswith('stages/') else None


def _load(path):
    try:
        with open(path, encoding='utf-8') as f:
            return json.load(f)
    except (OSError, ValueError):
        return None


def content_hash(path):
    """Hash of the file's JSON value (not its bytes): reformatting is not an edit."""
    data = _load(path)
    if data is None:
        return None
    text = json.dumps(data, ensure_ascii=False, sort_keys=True, separators=(',', ':'))
    return hashlib.sha256(text.encode('utf-8')).hexdigest()


def load_manifest():
    data = _load(MANIFEST) or {}
    return data.get('files', {})


def save_manifest(files):
    out = {
        "_comment": "Written by tools/gen_floors.py: the hash of every floor file as the generator last wrote it. "
                    "A file whose hash no longer matches was edited by a person; the generator asks before "
                    "replacing it (tools/floor_guard.py).",
        "files": dict(sorted(files.items())),
    }
    with open(MANIFEST, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
        f.write('\n')


class Staging:
    """A copy of assets/data/story/floors and assets/data/stages the generator writes into."""

    def __init__(self):
        self.root = tempfile.mkdtemp(prefix='toms_floors_')
        self.floors_dir = os.path.join(self.root, 'story', 'floors')
        self.stages_dir = os.path.join(self.root, 'stages')
        shutil.copytree(FLOORS_DIR, self.floors_dir)
        shutil.copytree(STAGES_DIR, self.stages_dir)

    def close(self):
        shutil.rmtree(self.root, ignore_errors=True)


def _files(base):
    out = []
    for sub in ('story/floors', 'stages'):
        d = os.path.join(base, sub)
        for name in sorted(os.listdir(d)):
            if name.endswith('.json'):
                out.append(sub + '/' + name)
    return out


def plan(staging):
    """(changes, hand_edited_floors): every file the new output changes, and the floors among them a
    person edited."""
    manifest = load_manifest()
    changes = []
    hand = set()
    for rel in _files(staging.root):
        new_path = os.path.join(staging.root, rel)
        old_path = os.path.join(DATA, rel)
        new_hash = content_hash(new_path)
        old_hash = content_hash(old_path) if os.path.exists(old_path) else None
        if new_hash == old_hash:
            continue
        changes.append(rel)
        if old_hash is not None and manifest.get(rel) != old_hash:
            n = floor_of(rel)
            hand.add(n if n is not None else rel)
    return changes, hand


def describe(hand):
    floors = sorted(x for x in hand if isinstance(x, int))
    others = sorted(x for x in hand if not isinstance(x, int))
    parts = []
    if floors:
        parts.append(('Floor ' if len(floors) == 1 else 'Floors ') + ', '.join(str(n) for n in floors))
    parts += others
    return '; '.join(parts)


def ask(hand, answer=None):
    """'yes' (replace them), 'keep' (skip those floors) or 'cancel'."""
    if not hand or answer:
        return answer or 'yes'
    what = describe(hand)
    text = ("%s %s changed by hand (in the stage editor or the files).\n\n"
            "The generator will place its own data there.\n\n"
            "Yes: replace them too\n"
            "No: keep your edits and skip those floors (their stairs may no longer line up with the next floor)\n"
            "Cancel: write nothing" % (what, 'was' if len(hand) == 1 else 'were'))
    if os.environ.get('TOMS_NO_POPUPS') != '1':
        try:
            import tkinter
            from tkinter import messagebox
            root = tkinter.Tk()
            root.withdraw()
            root.attributes('-topmost', True)
            r = messagebox.askyesnocancel('Floor generator', text, icon='warning', default='cancel', parent=root)
            root.destroy()
            return 'yes' if r is True else ('keep' if r is False else 'cancel')
        except Exception:
            pass   # no display: ask on the console
    print(text)
    if not sys.stdin or not sys.stdin.isatty():
        print('(no console to answer on: nothing written; pass --yes or --keep-edits)')
        return 'cancel'
    try:
        r = input('[y]es / [n]o / [c]ancel: ').strip().lower()
    except EOFError:
        r = ''
    return {'y': 'yes', 'yes': 'yes', 'n': 'keep', 'no': 'keep'}.get(r, 'cancel')


def _eol(path):
    try:
        with open(path, 'rb') as f:
            return '\r\n' if b'\r\n' in f.read() else '\n'
    except OSError:
        return '\n'


def apply(staging, changes, skip_floors, dry_run=False):
    """Copies the staged files over the real ones (except the skipped floors) and records their hashes."""
    manifest = load_manifest()
    written = []
    for rel in changes:
        n = floor_of(rel)
        if (n if n is not None else rel) in skip_floors:
            continue
        written.append(rel)
        if dry_run:
            continue
        src = os.path.join(staging.root, rel)
        dst = os.path.join(DATA, rel)
        eol = _eol(dst) if os.path.exists(dst) else '\n'
        with open(src, encoding='utf-8') as f:
            text = f.read().replace('\r\n', '\n')
        with open(dst, 'w', encoding='utf-8', newline=eol) as f:
            f.write(text)
    if not dry_run:
        # every file the generator now stands behind: the ones it wrote, and the unchanged ones that
        # already equal its output
        for rel in _files(staging.root):
            n = floor_of(rel)
            if (n if n is not None else rel) in skip_floors:
                continue
            h = content_hash(os.path.join(DATA, rel))
            if h is not None and h == content_hash(os.path.join(staging.root, rel)):
                manifest[rel] = h
        save_manifest(manifest)
    return written


def finish(staging, answer=None, dry_run=False):
    """Plan, ask, apply. Returns the process exit code (0 written / nothing to do, 1 cancelled)."""
    changes, hand = plan(staging)
    if not changes:
        print('floor_guard: the generator\'s output equals the data; nothing to write')
        if not dry_run:
            apply(staging, [], set())   # records the hashes
        return 0
    if hand:
        print('floor_guard: hand-edited: %s' % describe(hand))
    if dry_run:
        print('floor_guard: --dry-run: %d file(s) would change: %s' % (len(changes), ', '.join(changes)))
        return 0
    choice = ask(hand, answer)
    if choice == 'cancel':
        print('floor_guard: cancelled; nothing written')
        return 1
    skip = hand if choice == 'keep' else set()
    written = apply(staging, changes, skip)
    print('floor_guard: wrote %d file(s)%s' % (len(written), (', kept hand edits on %s' % describe(skip)) if skip else ''))
    if written:   # the per-floor event index follows the floors (Q10)
        import make_event_index
        text = make_event_index.text_of(make_event_index.build())
        with open(make_event_index.OUT, 'w', encoding='utf-8', newline=chr(10)) as f:
            f.write(text)
        print('floor_guard: rebuilt %s' % os.path.relpath(make_event_index.OUT, ROOT))
    return 0
