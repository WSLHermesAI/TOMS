#!/usr/bin/env python3
"""i18n_check.py -- the Qt editors' Chinese text: every tr("...") must have a translation.

The editors (atlas, anim, particle, font, toms_editor) write their UI in English through tr(); the
Language menu (tools/studio_common/Language.h) swaps it for Traditional Chinese from the JSON files in
tools/studio_common/i18n/zh_TW/ (English -> 中文, one file per editor, common.json for shared words).

    python tools/studio_common/i18n/i18n_check.py            report tr() strings with no translation
    python tools/studio_common/i18n/i18n_check.py --check    the same; exit 1 when one is missing (CTest)
    python tools/studio_common/i18n/i18n_check.py --stale    also list translations no tr() uses any more
    python tools/studio_common/i18n/i18n_check.py --missing-json   the missing ones as a JSON skeleton

A new tr() string: add it with its Chinese to the editor's file (or common.json). %1 %2 and & (the
menu shortcut letter) must stay in the translation.
"""
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
SOURCES = ['tools/atlas/qt', 'tools/anim/qt', 'tools/particle/qt', 'tools/font/qt', 'tools/studio_common',
           'src/editor/src', 'tools/stage_event_editor/app']
DICT_DIR = os.path.join(ROOT, 'tools', 'studio_common', 'i18n', 'zh_TW')

ESCAPES = {'n': '\n', 't': '\t', '"': '"', "'": "'", '\\': '\\', 'r': '\r', '0': '\0'}


def read_literal(s, i):
    """s[i] == '"': returns (text, index after the closing quote)."""
    out = []
    i += 1
    while i < len(s):
        c = s[i]
        if c == '\\' and i + 1 < len(s):
            n = s[i + 1]
            if n == 'x':
                m = re.match(r'[0-9a-fA-F]+', s[i + 2:])
                out.append(chr(int(m.group(0), 16)))
                i += 2 + len(m.group(0))
                continue
            if n == 'u':
                out.append(chr(int(s[i + 2:i + 6], 16)))
                i += 6
                continue
            out.append(ESCAPES.get(n, n))
            i += 2
            continue
        if c == '"':
            return ''.join(out), i + 1
        out.append(c)
        i += 1
    raise ValueError('unterminated string')


def skip_space(s, i):
    while i < len(s):
        if s[i] in ' \t\r\n':
            i += 1
        elif s.startswith('//', i):
            i = s.find('\n', i)
            i = len(s) if i < 0 else i
        elif s.startswith('/*', i):
            i = s.find('*/', i) + 2
        else:
            break
    return i


TR = re.compile(r'(?<![A-Za-z0-9_])tr\(\s*(?:u8)?"')


def extract(text):
    """Every tr("..." "...") first argument (adjacent literals joined) in a C++ source."""
    found = []
    for m in TR.finditer(text):
        i = m.end() - 1
        parts = []
        while True:
            lit, i = read_literal(text, i)
            parts.append(lit)
            i = skip_space(text, i)
            if text.startswith('u8"', i):
                i += 2
            if i < len(text) and text[i] == '"':
                continue
            break
        found.append(''.join(parts))
    return found


def strings():
    """tr() text -> the files using it."""
    out = {}
    for d in SOURCES:
        for f in sorted(glob.glob(os.path.join(ROOT, d, '**', '*.cpp'), recursive=True) +
                        glob.glob(os.path.join(ROOT, d, '**', '*.h'), recursive=True)):
            rel = os.path.relpath(f, ROOT).replace('\\', '/')
            if '/tests/' in rel:
                continue
            for s in extract(open(f, encoding='utf-8').read()):
                out.setdefault(s, set()).add(rel)
    return out


def dictionary():
    merged = {}
    for f in sorted(glob.glob(os.path.join(DICT_DIR, '*.json'))):
        merged.update(json.load(open(f, encoding='utf-8')))
    return merged


def placeholders(s):
    return sorted(re.findall(r'%\d', s))


def main():
    args = sys.argv[1:]
    src = strings()
    tr = dictionary()
    missing = sorted(s for s in src if s not in tr)
    bad = sorted(s for s in src if s in tr and placeholders(s) != placeholders(tr[s]))
    if '--missing-json' in args:
        json.dump({s: '' for s in missing}, sys.stdout, ensure_ascii=False, indent=1)
        return 0
    sys.stdout.reconfigure(encoding='utf-8')
    print('[i18n] %d tr() strings, %d translated, %d missing, %d with wrong %%N' %
          (len(src), len(src) - len(missing), len(missing), len(bad)))
    for s in missing:
        print('  missing: %r  (%s)' % (s, ', '.join(sorted(src[s]))))
    for s in bad:
        print('  %%N differs: %r -> %r' % (s, tr[s]))
    if '--stale' in args:
        for s in sorted(set(tr) - set(src)):
            print('  unused: %r' % s)
    return 1 if (missing or bad) and '--check' in args else 0


if __name__ == '__main__':
    sys.exit(main())
