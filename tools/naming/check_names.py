"""check_names.py -- no hard-coded name in the game's C++ (the owner, 2026-11-26; docs/project/12_CPP_NAMING.md §7).

The game code writes no name as a string. It reports, outside comments:
- a data file's field written as a string where a field is read or written (l_jJ.value("dark", 0.85f), l_jJ["next"],
  l_jJ.contains("run"), f_Str(l_jA, "stat"), {"label", ...}, f_TrParam(text, "n", v)): it is toms::field::g_psz...
  (src/core/game/core/field_keys.h);
- a UI name written as a string where the code hands one to RmlUi (RegisterMember("active", ...), Bind, GetElementById,
  ...): it is toms::ui::g_psz... (ui_names.h);
- any other literal equal to a name the headers hold: game_names.h (a status, a clip, a battle event, ...: toms::state::
  g_pszIdle, never "idle"), text_keys.h (a line of text: toms::text::g_pszBattleWordParry, never "battle.word_parry"),
  res_keys.h (a file: toms::key::g_pszSfxBossIntro, never "sfx.boss_intro") and ui_names.h.
Log and error text stay strings.

    python tools/naming/check_names.py        exit 0: none; 1: the list (file:line: "name" -> the constant)

A line that must keep the literal (one the checker cannot tell apart) carries the comment  // name-ok
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
CORE = os.path.join(ROOT, 'src', 'core', 'game', 'core')
HEADER = os.path.join(CORE, 'game_names.h')
FIELDS = os.path.join(CORE, 'field_keys.h')
# the headers whose every name the code must use by its constant (game_names.h's namespaces are read the same way)
NAME_HEADERS = [HEADER, os.path.join(CORE, 'text_keys.h'), os.path.join(CORE, 'res_keys.h')]
UI_HEADER = os.path.join(CORE, 'ui_names.h')
# the files that speak to the UI: there a literal equal to a UI name is one (elsewhere "player" or "gold" is something else)
UI_FILES = {'game_ui.cpp', 'game_player_menu.cpp'}
DIRS = [os.path.join(ROOT, 'src', 'core', 'game'), os.path.join(ROOT, 'src', 'game', 'src')]
SKIP = {'game_names.h', 'field_keys.h', 'res_keys.h', 'text_keys.h', 'ui_names.h'}
CONST = re.compile(r'\s*inline constexpr const char (\w+)\[\]\s*=\s*"([^"]*)"')


def f_ReadNames(l_vHeaders=None):
    """{value: ["toms::ns::g_pszX", ...]} from the headers."""
    names = {}
    for l_strH in (l_vHeaders or [HEADER]):
        ns = None
        for line in open(l_strH, encoding='utf-8'):
            m = re.match(r'\s*namespace\s+(\w+)\s*\{', line)
            if m and m.group(1) != 'toms':
                ns = m.group(1)
            m = CONST.match(line)
            if m and ns and ns != 'pattern':   # res_keys.h's patterns ("stage") are families, named by f_PathFor
                names.setdefault(m.group(2), []).append('toms::%s::%s' % (ns, m.group(1)))
    return names


def f_StripComments(text):
    """The text with comments blanked (strings kept; line numbers kept)."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == '\\' else 1
            out.append(text[i:j + 1]); i = j + 1
        elif text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            out.append(' ' * (j - i)); i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r'[^\n]', ' ', text[i:j])); i = j
        else:
            out.append(c); i += 1
    return ''.join(out)


# where a string is a data file's field: a JSON key (.value("k", / .contains("k") / ["k"] / .erase("k") / .count("k") /
# .at("k") / a helper reading one, f_Str(l_jA, "k")) or the key of a {"k", value} pair (a JSON literal, a text's
# {placeholder}, a name -> enum table)
KEY_AT = re.compile(r'((\.|->)value\(\s*|(\.|->)contains\(\s*|\[\s*|(\.|->)erase\(\s*|(\.|->)count\(\s*|(\.|->)at\(\s*|'
                    r'\bf_\w+\(\s*\*?l_[jp]\w+\s*,\s*)$')
PAIR_KEY = re.compile(r'(^|[{,])\s*\{\s*$')   # {{"k", v}} or , {"k", v}: a pair; = {"a", "b"} is a list
C_ARRAY = re.compile(r'\[\]\s*=\s*\{\s*$')            # const char* l_arr[] = { "a", "b" }: a list, not a pair
# where the code hands RmlUi a name its .rml / .rcss files use
UI_CALL = re.compile(r'\b(Bind|RegisterMember|CreateDataModel|GetDataModel|RemoveDataModel|BindEventCallback|'
                     r'GetElementById|QuerySelector|QuerySelectorAll|SetClass|IsClassSet|DirtyVariable)\(\s*$')
IDENT = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')
PLACEHOLDER_CALL = re.compile(r'\bf_TrParam\(')   # f_TrParam(text, "name", value): "name" is the text's {name}


def f_ArgIndex(seg):
    """Which argument of a call the text after its '(' ends in (None: the call closed)."""
    l_iDepth = l_iArg = 0
    for ch in re.sub(r'"(?:[^"\\]|\\.)*"', '""', seg):
        if ch in '([{':
            l_iDepth += 1
        elif ch in ')]}':
            l_iDepth -= 1
            if l_iDepth < 0:
                return None
        elif ch == ',' and l_iDepth == 0:
            l_iArg += 1
    return l_iArg


def f_IsPlaceholder(pre):
    for m in reversed(list(PLACEHOLDER_CALL.finditer(pre))):
        l_iArg = f_ArgIndex(pre[m.end():])
        if l_iArg is not None:
            return l_iArg == 1 and re.search(r',\s*$', pre) is not None
    return False


def f_ReadFields():
    """{field name: "toms::field::g_pszX"} from field_keys.h."""
    out = {}
    for line in open(FIELDS, encoding='utf-8'):
        m = CONST.match(line)
        if m:
            out[m.group(2)] = 'toms::field::' + m.group(1)
    return out


def main():
    names, fields, ui = f_ReadNames(NAME_HEADERS), f_ReadFields(), f_ReadNames([UI_HEADER])
    found = []
    for base in DIRS:
        for dp, _, fs in os.walk(base):
            for f in fs:
                if not f.endswith(('.cpp', '.h')) or f.endswith('_test.cpp') or f in SKIP:
                    continue
                path = os.path.join(dp, f)
                raw = open(path, encoding='utf-8', errors='replace').read()
                rawlines = raw.split('\n')
                code = f_StripComments(raw).split('\n')
                for no, line in enumerate(code, 1):
                    if '// name-ok' in rawlines[no - 1]:
                        continue
                    for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', line):
                        v, pre = m.group(1), line[:m.start()]
                        where = '%s:%d: "%s"' % (os.path.relpath(path, ROOT).replace('\\', '/'), no, v)
                        if UI_CALL.search(pre):
                            found.append('%s -> %s' % (where, ' / '.join(ui.get(v, [])) or 'a new line in ui_names.h'))
                            continue
                        is_key = KEY_AT.search(pre) or f_IsPlaceholder(pre) or (PAIR_KEY.search(pre) and not C_ARRAY.search(pre) and re.match(r'\s*,', line[m.end():]))
                        if is_key and IDENT.match(v):
                            found.append('%s -> %s' % (where, fields.get(v, 'a new line in field_keys.h')))
                        elif v in names:
                            found.append('%s -> %s' % (where, ' / '.join(names[v])))
                        elif f in UI_FILES and v in ui:
                            found.append('%s -> %s' % (where, ' / '.join(ui[v])))
    for l in found:
        print(l)
    print('[check_names] %d hard-coded name(s) in the game code' % len(found))
    return 1 if found else 0


if __name__ == '__main__':
    sys.exit(main())
