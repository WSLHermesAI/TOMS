#!/usr/bin/env python3
"""rename_cpp.py -- give every C++ variable and function of the project its name by docs/project/12_CPP_NAMING.md.

    python tools/naming/rename_cpp.py plan  [--jobs 8] [--only <path part>]   (writes Build/naming/plan.json + report.txt)
    python tools/naming/rename_cpp.py apply                                     (applies Build/naming/plan.json)

The C++ is read with libclang (pip install libclang), each file with its own compile flags from the builds' compile
databases (Build/windows-release, plus Build/web-release-windows and Build/android-debug for the code inside
#ifdef __EMSCRIPTEN__ / __ANDROID__). Every declaration of a variable or a function in the project's own files, and
every reference to it, is found by its clang USR; the new name comes from the declaration's scope and its real type:

    member m_, local and parameter l_, global g_, then the type prefix (str, i, b, v, map, sp, o, ...); function f_.

Third-party code (src/third_party, the downloaded libraries under Build/_deps-src, AIGamestyle, Qt, the SDKs) is
never edited, and names it decides are kept: overrides of its virtual functions, functions it declares, extern "C"
functions, and names found by lookup (begin, end, size, to_json, ...). The report lists every kept name and why,
the names two declarations would share, and the references clang could not resolve (inside templates), which the
build then shows.
"""
import argparse
import ctypes
import json
import multiprocessing
import os
import re
import shlex
import sys
import time
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Build' / 'naming'
# TOMS_NAMING_WIN_DB=windows-debug: the Debug flags (assert bodies and #ifdef _DEBUG code are compiled only there)
DBS = [('win', ROOT / 'Build' / os.environ.get('TOMS_NAMING_WIN_DB', 'windows-release') / 'compile_commands.json'),
       ('web', ROOT / 'Build' / 'web-release-windows' / 'compile_commands.json'),
       ('android', ROOT / 'Build' / 'android-debug' / 'compile_commands.json'),
       # the web atlas tool's own project (tools\build_atlas_web.cmd; its database: ninja -C Build/atlas-web -t compdb)
       ('web', ROOT / 'Build' / 'atlas-web' / 'compile_commands.json')]
EMSDK_SYSROOT = 'D:/Work/emsdk/upstream/emscripten/cache/sysroot'
EXCLUDE = ('src/third_party/', 'build/', 'aigamestyle/', 'android/app/build/')   # (lower case: rel())
CPP_EXT = ('.cpp', '.cc', '.c', '.h', '.hpp', '.inl')

# names a library or the language finds by name
LIB_NAMESPACES = {'std', 'glm', 'nlohmann', 'Rml', 'bgfx', 'bx', 'SDL', 'ImGui', 'JPH', 'cgltf', 'tweeny', 'Qt'}
ALLOCATOR_METHODS = {'allocate', 'deallocate', 'construct', 'destroy', 'max_size'}   # (a member of an STL allocator)
KEEP_FUNCS = {'main', 'wmain', 'WinMain', 'wWinMain', 'android_main', 'SDL_AppInit', 'SDL_AppIterate', 'SDL_AppEvent',
              'SDL_AppQuit', 'to_json', 'from_json', 'begin', 'end', 'cbegin', 'cend', 'rbegin', 'rend', 'size', 'swap',
              'hash'}

RECORD_PREFIX = [  # (namespace marker, template/class name) -> prefix
    ('basic_string', 'str'), ('string', 'str'), ('wstring', 'str'), ('basic_string_view', 'sv'), ('string_view', 'sv'), ('vector', 'v'), ('array', 'arr'), ('map', 'map'),
    ('unordered_map', 'map'), ('multimap', 'map'), ('set', 'set'), ('unordered_set', 'set'), ('multiset', 'set'),
    ('list', 'list'), ('deque', 'dq'), ('pair', 'pair'), ('tuple', 'tup'), ('optional', 'opt'),
    ('shared_ptr', 'sp'), ('unique_ptr', 'up'), ('weak_ptr', 'wp'), ('function', 'fn')]
TYPEDEF_PREFIX = {'size_t': 'n', 'int8_t': 'i8', 'int16_t': 'i16', 'int32_t': 'i', 'int64_t': 'i64', 'uint8_t': 'u8',
                  'uint16_t': 'u16', 'uint32_t': 'u', 'uint64_t': 'u64', 'uintptr_t': 'u64', 'intptr_t': 'i64',
                  'ptrdiff_t': 'i64', 'Uint8': 'u8', 'Uint16': 'u16', 'Uint32': 'u', 'Uint64': 'u64', 'Sint8': 'i8',
                  'Sint16': 'i16', 'Sint32': 'i', 'Sint64': 'i64'}
PREFIXES = sorted({'b', 'i', 'i8', 'i16', 'i64', 'u', 'u8', 'u16', 'u64', 'n', 'f', 'd', 'c', 'str', 'sv', 'psz', 'v',
                   'arr', 'map', 'set', 'list', 'dq', 'pair', 'tup', 'opt', 'p', 'sp', 'up', 'wp', 'fn', 'it', 'e', 'j',
                   'v2', 'v3', 'v4', 'mat3', 'mat4', 'q', 'o'}, key=len, reverse=True)
CPP_KEYWORDS = {'and', 'or', 'not', 'new', 'delete', 'this', 'class', 'struct'}


ROOT_NC = os.path.normcase(str(ROOT))


def rel(path):
    """The path under the project, in lower case (Windows: the compile databases and the #includes spell one file
    differently), or None."""
    p = os.path.normcase(os.path.abspath(path))
    if not p.startswith(ROOT_NC + os.sep):
        return None
    return p[len(ROOT_NC) + 1:].replace(os.sep, '/')


def is_project(path):
    r = rel(path) if path else None
    return bool(r) and not r.startswith(EXCLUDE) and r.endswith(CPP_EXT)


# ---------------------------------------------------------------------------------------------------------------
# collecting (one translation unit per worker call)
# ---------------------------------------------------------------------------------------------------------------

def tu_args(config, entry):
    cmd = entry.get('arguments') or shlex.split(entry['command'], posix=False)
    # a whole argument in quotes loses them; an escaped quote inside (-DNAME=\\"text\\") becomes a plain one
    cmd = [(c[1:-1] if len(c) > 1 and c[0] == c[-1] == '"' else c).replace('\\"', '"') for c in cmd]
    src = os.path.normcase(os.path.abspath(os.path.join(entry['directory'], entry['file'])))
    out, skip = [], False
    for a in cmd[1:]:
        if skip:
            skip = False
            continue
        if a in ('-c', '-o', '/c') or a.startswith(('/Fo', '/Fd', '/FS', '-Fo', '-Fd', '/showIncludes', '@')):
            skip = a in ('-o',)
            continue
        if os.path.normcase(os.path.abspath(os.path.join(entry['directory'], a))) == src:
            continue
        out.append(a)
    if config == 'win':
        # (the newest MSVC headers use a builtin this libclang does not have)
        out = ['--driver-mode=cl', '-D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH', '/clang:-Wno-everything',
               '-D__builtin_verbose_trap(a,b)=__debugbreak()'] + out
    elif config == 'web':
        out = ['--target=wasm32-unknown-emscripten', '--sysroot=' + EMSDK_SYSROOT, '-D__EMSCRIPTEN__',
               '-isystem', EMSDK_SYSROOT + '/include/compat', '-Wno-everything'] + out
    else:
        out = ['-Wno-everything', '-resource-dir', str(NDK_CLANG_DIR)] + out
    return src, out


def libclang_path():
    """A libclang as new as the headers (the MSVC STL, emscripten's and the NDK's libc++ need clang 19+): TOMS_LIBCLANG,
    else Visual Studio's (VC/Tools/Llvm), else the pip package's own."""
    if os.environ.get('TOMS_LIBCLANG'):
        return os.environ['TOMS_LIBCLANG']
    found = sorted(Path(os.environ.get('ProgramFiles', r'C:\Program Files'), 'Microsoft Visual Studio').glob(
        '*/*/VC/Tools/Llvm/x64/bin/libclang.dll'))
    return str(found[-1]) if found else None


NDK_CLANG_DIR = Path(os.environ.get('LOCALAPPDATA', '')) / 'Android/Sdk/ndk/27.2.12479018/toolchains/llvm/prebuilt/windows-x86_64/lib/clang/18'


def collect(job):
    import clang.cindex as ci
    # Android: the pip package's libclang (clang 18, the NDK's own version; the NDK's libc++ does not parse with a
    # newer one). Windows and web: Visual Studio's. (One libclang per process: the two run in separate pools.)
    if not ci.Config.loaded and job[0] != 'android' and libclang_path():
        ci.Config.set_library_file(libclang_path())
    config, entry = job
    src, args = tu_args(config, entry)
    index = ci.Index.create()
    try:
        tu = index.parse(src, args=args, options=ci.TranslationUnit.PARSE_DETAILED_PROCESSING_RECORD)
    except ci.TranslationUnitLoadError as e:
        return {'src': src, 'error': str(e)}
    lib = ci.conf.lib
    spell = lib.clang_getSpellingLocation
    spell.argtypes = [ci.SourceLocation, ctypes.POINTER(ci.c_object_p), ctypes.POINTER(ctypes.c_uint),
                      ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
    spell.restype = None
    files = {}
    texts = {}

    def where(loc):
        f, a, b, off = ci.c_object_p(), ctypes.c_uint(), ctypes.c_uint(), ctypes.c_uint()
        spell(loc, ctypes.byref(f), ctypes.byref(a), ctypes.byref(b), ctypes.byref(off))
        if not f:
            return None, 0
        name = ci.File(f).name
        r = files.get(name)
        if r is None:
            r = files[name] = (rel(name) if is_project(name) else '')
        return r, off.value

    def token_ok(r, off, name):
        t = texts.get(r)
        if t is None:
            t = texts[r] = (ROOT / r).read_bytes()
        b = name.encode()
        if t[off:off + len(b)] != b:
            return False
        before = t[off - 1:off] if off > 0 else b' '
        after = t[off + len(b):off + len(b) + 1]
        ident = re.compile(rb'[A-Za-z0-9_]')
        return not ident.match(before) and not ident.match(after or b' ')

    K = ci.CursorKind
    DECL = {K.VAR_DECL: 'var', K.FIELD_DECL: 'field', K.PARM_DECL: 'parm', K.FUNCTION_DECL: 'func',
            K.CXX_METHOD: 'method', K.FUNCTION_TEMPLATE: 'func'}
    REF = {K.DECL_REF_EXPR, K.MEMBER_REF_EXPR, K.MEMBER_REF, K.VARIABLE_REF, K.OVERLOADED_DECL_REF,
           K.USING_DECLARATION}
    FUNC_PARENTS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.FUNCTION_TEMPLATE,
                    K.CONVERSION_FUNCTION, K.LAMBDA_EXPR}
    CLASS_PARENTS = {K.CLASS_DECL, K.STRUCT_DECL, K.UNION_DECL, K.CLASS_TEMPLATE,
                     K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION}
    decls, refs, deps, ext_virtual, reflocs, type_names = {}, {}, {}, set(), {}, set()
    base_cache = {}

    def ext_base_has(cls, name, depth=0):
        if cls is None or depth > 12:
            return False
        key = (cls.get_usr() or cls.displayname, name)
        if key in base_cache:
            return base_cache[key]
        base_cache[key] = False
        found = False
        for b in cls.get_children():
            if b.kind != K.CXX_BASE_SPECIFIER:
                continue
            bd = b.referenced or b.type.get_declaration()
            if bd is None or bd.kind == K.NO_DECL_FOUND:
                continue
            bd = bd.get_definition() or bd
            if not is_project(bd.location.file.name if bd.location.file else None):
                if any(c.spelling == name and c.kind in (K.CXX_METHOD, K.FUNCTION_TEMPLATE) for c in bd.get_children()):
                    found = True
                    break
            if ext_base_has(bd, name, depth + 1):
                found = True
                break
        base_cache[key] = found
        return found

    def prefix_of(t):
        if t.kind in (ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.RVALUEREFERENCE):
            t = t.get_pointee()
        u = t
        for _ in range(10):   # typedef names that say more than the canonical type (size_t, uint32_t)
            sp = re.sub(r'^(const |volatile )+', '', u.spelling).split('::')[-1].strip()
            if sp in TYPEDEF_PREFIX:
                return TYPEDEF_PREFIX[sp]
            if u.kind in (ci.TypeKind.TYPEDEF, ci.TypeKind.ELABORATED):
                nxt = u.get_declaration().underlying_typedef_type if u.kind == ci.TypeKind.TYPEDEF else u.get_named_type()
                if nxt.kind == ci.TypeKind.INVALID:
                    break
                u = nxt
            else:
                break
        c = t.get_canonical()
        k = c.kind
        TK = ci.TypeKind
        simple = {TK.BOOL: 'b', TK.CHAR_S: 'c', TK.CHAR_U: 'c', TK.SCHAR: 'i8', TK.UCHAR: 'u8', TK.WCHAR: 'c',
                  TK.CHAR16: 'c', TK.CHAR32: 'c', TK.SHORT: 'i', TK.INT: 'i', TK.LONG: 'i', TK.LONGLONG: 'i64',
                  TK.USHORT: 'u16', TK.UINT: 'u', TK.ULONG: 'u', TK.ULONGLONG: 'u64', TK.FLOAT: 'f', TK.DOUBLE: 'd',
                  TK.LONGDOUBLE: 'd', TK.ENUM: 'e'}
        if k in simple:
            return simple[k]
        if k == TK.POINTER:
            pk = c.get_pointee().get_canonical().kind
            if pk in (TK.CHAR_S, TK.CHAR_U, TK.SCHAR, TK.WCHAR, TK.CHAR16, TK.CHAR32):
                return 'psz'
            if pk in (TK.FUNCTIONPROTO, TK.FUNCTIONNOPROTO):
                return 'fn'
            return 'p'
        if k == TK.MEMBERPOINTER:
            return 'fn' if c.get_pointee().kind in (TK.FUNCTIONPROTO, TK.FUNCTIONNOPROTO) else 'p'
        if k in (TK.CONSTANTARRAY, TK.INCOMPLETEARRAY, TK.VARIABLEARRAY, TK.DEPENDENTSIZEDARRAY):
            ek = c.element_type.get_canonical().kind
            return 'psz' if ek in (TK.CHAR_S, TK.CHAR_U, TK.WCHAR, TK.CHAR16, TK.CHAR32) else 'arr'   # text: a C string
        s = c.spelling
        lead = re.sub(r'^(const |volatile |struct |class )+', '', s)
        m = re.match(r'std::(?:__\w+::)?(\w+)', lead)
        if m:   # the outermost type decides: std::vector<glm::vec3> is a vector
            if 'iterator' in m.group(1).lower():
                return 'it'
            for name, p in RECORD_PREFIX:
                if m.group(1) == name:
                    return p
            return 'o'
        m = re.match(r'glm::vec<(\d)', lead)
        if m:
            return 'v' + m.group(1)
        m = re.match(r'glm::mat<(\d), (\d)', lead)
        if m:
            return 'mat' + m.group(1)
        if lead.startswith('glm::qua<'):
            return 'q'
        if re.match(r'nlohmann::(?:\w+::)*basic_json<', lead):
            return 'j'
        if lead.startswith('(lambda') or '(lambda at' in lead:
            return 'fn'
        return 'o'   # any other class or struct, a template parameter, an undeduced auto

    def scope_of(cur, kind):
        if kind in ('func', 'method'):
            return 'f'
        if kind in ('field',):
            return 'm'
        if kind in ('parm', 'bind'):
            return 'l'
        p = cur.semantic_parent
        if p is not None and p.kind in CLASS_PARENTS:
            return 'm'
        lp = cur.lexical_parent
        if (p is not None and p.kind in FUNC_PARENTS) or (lp is not None and lp.kind in FUNC_PARENTS):
            return 'l'
        if p is not None and p.kind in (K.NAMESPACE, K.TRANSLATION_UNIT, K.LINKAGE_SPEC, K.UNEXPOSED_DECL):
            # a local inside a function has the function as its semantic parent; anything else here is global
            return 'g'
        return 'l'

    stack = [(tu.cursor, False)]
    while stack:
        cur, in_c = stack.pop()
        for ch in cur.get_children():
            loc = ch.location
            fname = loc.file.name if loc.file else None
            if ch.kind in (K.NAMESPACE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL, K.CLASS_DECL, K.STRUCT_DECL,
                           K.CLASS_TEMPLATE, K.FUNCTION_DECL, K.VAR_DECL, K.CXX_METHOD, K.FUNCTION_TEMPLATE,
                           K.TYPEDEF_DECL, K.ENUM_DECL, K.UNION_DECL, K.TYPE_ALIAS_DECL) and cur.kind in (
                    K.TRANSLATION_UNIT, K.NAMESPACE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL) and not is_project(fname):
                continue   # a third-party header's declarations
            kind = DECL.get(ch.kind)
            if ch.kind == K.UNEXPOSED_DECL and cur.kind == K.UNEXPOSED_DECL and cur.spelling.startswith('['):
                kind = 'bind'   # auto& [key, value]: each name a local
            if kind and fname:
                r, off = where(loc)
                name = ch.spelling
                if r and name:
                    usr = ch.get_usr()
                    keep = None
                    if not token_ok(r, off, name):
                        keep = 'declared through a macro'
                    elif kind in ('func', 'method'):
                        if name in KEEP_FUNCS or name.startswith('operator'):
                            keep = 'found by name (language / library)'
                        elif kind == 'method' and name in ALLOCATOR_METHODS:
                            keep = 'found by name (the standard allocator protocol)'
                        elif in_c or (ch.linkage == ci.LinkageKind.EXTERNAL and ch.kind == K.FUNCTION_DECL and
                                      any(t.spelling == 'EMSCRIPTEN_KEEPALIVE' for t in ch.get_tokens())):
                            keep = 'extern "C" / called by name from JavaScript or Java'
                        else:
                            can = ch.canonical
                            cf = can.location.file.name if can.location.file else None
                            if cf and not is_project(cf):
                                keep = 'declared by third-party code'
                            elif kind == 'method' and ch.is_virtual_method() and ext_base_has(ch.semantic_parent, name):
                                keep = 'overrides a third-party virtual function'
                                ext_virtual.add(name)
                    elif kind in ('var', 'field'):
                        can = ch.canonical
                        cf = can.location.file.name if can.location.file else None
                        if cf and not is_project(cf):
                            keep = 'declared by third-party code'
                    if usr:
                        d = decls.setdefault(usr, {'name': name, 'kind': kind, 'scope': scope_of(ch, kind),
                                                   'prefix': prefix_of(ch.type) if kind not in ('func', 'method') else '',
                                                   'parent': (ch.semantic_parent.get_usr() or '') if ch.semantic_parent else '',
                                                   'virtual': kind == 'method' and ch.is_virtual_method(),
                                                   'keep': None, 'at': []})
                        if keep and not d['keep']:
                            d['keep'] = keep
                        d['at'].append((r, off))
            elif ch.kind in REF and fname:
                target = ch.referenced
                name = ch.spelling
                if ch.kind == K.OVERLOADED_DECL_REF and target is None and hasattr(ch, 'get_num_overloaded_decls')                         and ch.get_num_overloaded_decls() > 0:
                    target = ch.get_overloaded_decl(0)
                if target is not None and (target.kind in DECL or target.kind == K.UNEXPOSED_DECL) and name and                         target.spelling == name:
                    r, off = where(loc)
                    if r and token_ok(r, off, name):
                        usr = target.get_usr()
                        if usr:
                            refs.setdefault(usr, set()).add((r, off))
                        # a template's instantiation (Key<int>::t, firstTime<vec3>) has its own USR but the template's
                        # place in the source: the reference is matched to the declaration there as well
                        tr, toff = where(target.location)
                        if tr:
                            reflocs.setdefault((tr, toff), set()).add((r, off))
                elif ch.kind in (K.MEMBER_REF_EXPR, K.DECL_REF_EXPR, K.OVERLOADED_DECL_REF, K.USING_DECLARATION) and (
                        target is None or target.kind == K.OVERLOADED_DECL_REF):
                    # inside a template: a member of a dependent type (k.t), a call with dependent arguments
                    # (lerpValue(a, b)). No target: the name is the expression's last token.
                    toks = list(ch.get_tokens())
                    if toks and toks[-1].kind == ci.TokenKind.IDENTIFIER:
                        tname = toks[-1].spelling
                        r, off = where(toks[-1].location)
                        if r and token_ok(r, off, tname):
                            base = ''
                            if ch.kind == K.MEMBER_REF_EXPR:
                                kids = list(ch.get_children())
                                base = kids[0].type.spelling if kids else ''
                            what = 'member' if ch.kind == K.MEMBER_REF_EXPR else 'call'
                            nxt = texts[r][off + len(tname):off + len(tname) + 40].lstrip()[:1]
                            qual = re.search(rb'(\w+)\s*::\s*$', texts[r][max(0, off - 60):off])
                            if what == 'call' and qual and qual.group(1).decode() in LIB_NAMESPACES:
                                continue   # std::forward, glm::mix: a library's own function
                            if what == 'member' or nxt in (b'(', b'<') or ch.kind == K.USING_DECLARATION:
                                deps.setdefault(tname, set()).add((r, off, what, base))
            elif ch.kind in (K.TYPE_REF, K.TEMPLATE_REF) and ch.referenced is not None:
                type_names.add(ch.referenced.spelling.split('::')[-1])
            stack.append((ch, in_c or ch.kind == K.LINKAGE_SPEC))
    return {'src': src, 'config': config, 'decls': decls,
            'refs': {u: sorted(v) for u, v in refs.items()}, 'deps': {n: sorted(v) for n, v in deps.items()},
            'reflocs': [[list(k), sorted(v)] for k, v in reflocs.items()], 'types': sorted(type_names),
            'ext_virtual': sorted(ext_virtual),
            'diag': [str(d) for d in tu.diagnostics if d.severity >= 3][:5]}


# ---------------------------------------------------------------------------------------------------------------
# planning
# ---------------------------------------------------------------------------------------------------------------

def camel(old, scope):
    s = old.strip('_')
    for p in ('m_', 'g_', 's_', 'l_', 'f_'):
        if s.startswith(p) and len(s) > 2:
            s = s[2:]
    if re.match(r'k[A-Z0-9]', s):
        s = s[1:]
    parts = [p for p in re.split(r'_+', s) if p]
    s = ''.join(p[0].upper() + p[1:] for p in parts) if parts else 'X'
    return s


# Collisions resolved by hand: (a part of the enclosing function's USR, the old name) -> the name's body. Each function
# had a capital and a small letter as two variables (W and w), which the rule would give one name.
OVERRIDES = {('buildUiState', 'W'): 'ScreenW', ('buildUiState', 'H'): 'ScreenH',
             ('drawStylingSpike#', 'W'): 'ScreenW', ('drawStylingSpike#', 'H'): 'ScreenH',
             ('importPlist', 'W'): 'TexW', ('importPlist', 'H'): 'TexH',
             ('convertMpdiWith', 'R'): 'Report'}


def new_name(d):
    old, scope = d['name'], d['scope']
    for (where, name), body in OVERRIDES.items():
        if old == name and where in d['parent'] and scope != 'f':
            return '%s_%s%s' % (scope, d['prefix'] or 'o', body)
    if scope == 'f':
        if re.match(r'f_[A-Z0-9]', old):
            return old
        return 'f_' + camel(old, scope)
    pre = d['prefix'] or 'o'
    if re.match(r'%s_%s[A-Z0-9]' % (scope, re.escape(pre)), old):
        return old
    return '%s_%s%s' % (scope, pre, camel(old, scope))


def jobs_for(only):
    jobs, seen_win = [], set()
    for config, path in DBS:
        if not path.exists():
            continue
        for e in json.load(open(path, encoding='utf-8')):
            f = os.path.abspath(os.path.join(e['directory'], e['file']))
            r = rel(f)
            if not r or r.startswith(EXCLUDE) or not r.endswith(CPP_EXT):
                continue
            if only and only not in r:
                continue
            if config == 'win':
                seen_win.add(r)
                jobs.append((config, e))
            else:
                # the other platforms: only the files with code the Windows build does not compile
                text = (ROOT / r).read_text(encoding='utf-8', errors='replace')
                if r not in seen_win or re.search(r'__EMSCRIPTEN__|__ANDROID__', text):
                    jobs.append((config, e))
    return jobs


def plan(a):
    OUT.mkdir(parents=True, exist_ok=True)
    jobs = jobs_for(a.only)
    print(f'{len(jobs)} translation units', flush=True)
    t0 = time.time()
    decls, refs, deps, ext_virtual, errors, seen = {}, defaultdict(set), defaultdict(set), set(), [], set()
    reflocs, type_names = defaultdict(set), set()
    results = []
    for group in ([j for j in jobs if j[0] != 'android'], [j for j in jobs if j[0] == 'android']):
        if not group:
            continue
        with multiprocessing.Pool(a.jobs) as pool:
            for i, res in enumerate(pool.imap_unordered(collect, group, chunksize=1)):
                results.append(res)
                if (len(results)) % 20 == 0:
                    print(f'  {len(results)}/{len(jobs)} ({time.time() - t0:.0f} s)', flush=True)
    # the Windows build first: where the platforms disagree on a token, Windows decides; web and Android only add
    # the code inside their own #ifdef blocks
    rank = {'win': 0, 'web': 1, 'android': 2}
    results.sort(key=lambda r: rank.get(r.get('config'), 3))
    if True:
        for res in results:
            if 'error' in res:
                errors.append(f"{res['src']}: {res['error']}")
                continue
            if res['diag']:
                errors.append(f"{res['config']} {res['src']}: " + ' | '.join(res['diag'][:2]))
            for usr, d in res['decls'].items():
                cur = decls.get(usr)
                if cur is None:
                    decls[usr] = dict(d, at=set(map(tuple, d['at'])), rank=rank.get(res['config'], 3))
                else:
                    cur['at'] |= set(map(tuple, d['at']))
                    if d['keep'] and not cur['keep']:
                        cur['keep'] = d['keep']
                    if cur['prefix'] == 'o' and d['prefix'] not in ('', 'o'):
                        cur['prefix'] = d['prefix']
                for r, off in d['at']:
                    seen.add(r)
            for usr, v in res['refs'].items():
                refs[usr] |= set(map(tuple, v))
            for n, v in res['deps'].items():
                deps[n] |= set(map(tuple, v))
            for k, v in res['reflocs']:
                reflocs[tuple(k)] |= set(map(tuple, v))
            type_names |= set(res['types'])
            ext_virtual |= set(res['ext_virtual'])
    # references to instantiations: to the declaration at the same place in the source (the template)
    at_index = {}
    for usr, d in decls.items():
        for key in d['at']:
            at_index.setdefault(key, usr)
    for key, toks in reflocs.items():
        usr = at_index.get(key)
        if usr:
            refs[usr] |= toks
    # a virtual function named like an overridden third-party one keeps its name everywhere (one override family)
    for d in decls.values():
        if d['kind'] == 'method' and d['virtual'] and d['name'] in ext_virtual and not d['keep']:
            d['keep'] = 'virtual, same name as an overridden third-party function'
    rename, kept = {}, defaultdict(list)
    for usr, d in decls.items():
        if d['keep']:
            kept[d['keep']].append(d['name'])
            continue
        n = new_name(d)
        if n != d['name']:
            rename[usr] = n
    # collisions: two different old names becoming one new name in one scope
    by_scope = defaultdict(lambda: defaultdict(set))
    for usr, n in rename.items():
        by_scope[decls[usr]['parent']][n].add(decls[usr]['name'])
    collisions = [(p, n, sorted(o)) for p, m in by_scope.items() for n, o in m.items() if len(o) > 1 and p]
    for p, n, olds in collisions:   # the rest: the later old names get a number (and the report says so)
        for k, old in enumerate(olds[1:], start=2):
            for usr in [u for u, nn in rename.items() if nn == n and decls[u]['parent'] == p and decls[u]['name'] == old]:
                rename[usr] = n + str(k)
    # the edits
    edits, clash, claimed = {}, [], {}
    # every token the Windows parse saw belongs to Windows, renamed or kept: another platform's view of the same
    # declaration (another USR when a type differs, size_t on wasm) never renames it
    win_tokens = set()
    for usr, d in decls.items():
        if d['rank'] == 0:
            win_tokens |= d['at'] | refs.get(usr, set())
    for usr, n in sorted(rename.items(), key=lambda kv: decls[kv[0]]['rank']):
        old, rk = decls[usr]['name'], decls[usr]['rank']
        for r, off in decls[usr]['at'] | refs.get(usr, set()):
            key = (r, off)
            if rk > 0 and key in win_tokens:
                continue
            if key in edits and edits[key][1] != n:
                if claimed[key] == rk:   # (a later platform's view of a token Windows already named: Windows wins)
                    clash.append((r, off, old, edits[key][1], n))
                continue
            edits[key] = (old, n)
            claimed[key] = rk
    # a kept declaration's tokens are never renamed through another platform's view of it
    for usr, d in decls.items():
        if d['keep']:
            for key in d['at']:
                edits.pop(key, None)
    # dependent references (inside templates), by name
    STD_MEMBERS = {'front', 'back', 'push_back', 'emplace_back', 'pop_back', 'clear', 'find', 'insert', 'erase', 'count',
                   'at', 'first', 'second', 'x', 'y', 'z', 'w', 'value', 'length', 'reserve', 'resize', 'c_str', 'str',
                   'substr', 'append', 'contains', 'emplace', 'try_emplace', 'get', 'reset', 'release', 'swap', 'data',
                   'empty', 'size', 'begin', 'end', 'load', 'store', 'lock', 'unlock', 'wait', 'notify_one', 'join'}
    kept_names = {d['name'] for d in decls.values() if d['keep']}
    project_classes = set()
    for d in decls.values():
        project_classes |= set(re.findall(r'@(?:S|ST>[^@]*|C|U)@(\w+)', d['parent']))
    by_old = defaultdict(set)   # old name -> {(kind, scope, parent, new)}
    for u, n in rename.items():
        d = decls[u]
        by_old[d['name']].add((d['kind'], d['scope'], d['parent'], n))
    dep_left, dep_done = {}, 0
    for name, toks in deps.items():
        cands = by_old.get(name)
        if not cands or name in type_names:   # (a type's name: using nlohmann::json, json{...})
            continue
        for r, off, what, base in toks:
            key = (r, off)
            if key in edits or key in win_tokens:
                continue
            new = None
            if what == 'call':
                fn = {n for k, sc, p, n in cands if sc == 'f'}
                if len(fn) == 1 and name not in kept_names:
                    new = fn.pop()
            else:
                if re.search(r'\b(std|glm|Rml|Qt?)\b::|^(const )?(std|glm)::', base):
                    continue
                mem = {(p, n) for k, sc, p, n in cands if sc in ('m', 'f') and k in ('field', 'var', 'method')}
                cls = re.findall(r'(\w+)\s*<', base) or re.findall(r'(\w+)\s*$', base.replace('&', '').strip())
                if cls and cls[0] not in project_classes and cls[0] not in ('auto', 'T'):
                    continue   # a member of a library type (json.get<T>(), a Qt or std object)
                if (base in ('<dependent type>', '') or (cls and cls[0] in ('auto', 'T'))) and name not in STD_MEMBERS:
                    # an unknown base inside a template: the class templates' members of that name, if they agree
                    tmpl = {n for p, n in mem if '@ST>' in p}
                    if len(tmpl) == 1:
                        new = tmpl.pop()
                if new is None and cls and cls[0] in project_classes:
                    own = {n for p, n in mem if re.search(r'@(S|ST>[^@]*|C)@' + re.escape(cls[0]) + r'\b', p)}
                    if len(own) == 1:
                        new = own.pop()
                if new is None and name not in STD_MEMBERS and name not in kept_names:
                    names = {n for p, n in mem}
                    if len(names) == 1:
                        new = names.pop()
            if new:
                edits[key] = (name, new)
                dep_done += 1
            else:
                dep_left.setdefault(name, []).append((r, off, base))
    dep_left = {n: v[:4] for n, v in dep_left.items()}
    project_files = sorted(r for r in (rel(p) for p in ROOT.rglob('*') if p.suffix in CPP_EXT)
                           if r and not r.startswith(EXCLUDE) and not r.startswith(('tools/naming', 'build/')))
    unseen = [r for r in project_files if r not in seen and not any(r == e[0] for e in edits)]
    plan_data = {'edits': [[r, off, o, n] for (r, off), (o, n) in sorted(edits.items())]}
    (OUT / 'plan.json').write_text(json.dumps(plan_data), encoding='utf-8')
    lines = [f'translation units: {len(jobs)}, parse problems: {len(errors)}',
             f'declarations: {len(decls)}, renamed: {len(rename)}, kept: {sum(len(v) for v in kept.values())}',
             f'edits: {len(edits)} in {len({r for r, _ in edits})} files', '']
    lines += ['== kept, by reason'] + [f'{k}: {len(v)}: ' + ', '.join(sorted(set(v))[:80]) for k, v in kept.items()]
    lines += ['', f'== collisions ({len(collisions)})'] + [f'{n}: {o}  (in {p})' for p, n, o in collisions]
    lines += ['', f'== one token, two new names ({len(clash)})'] + [str(c) for c in clash[:200]]
    other = sorted({(decls[u]['name'], n, r) for u, n in rename.items() if decls[u]['rank'] > 0
                    for r, off in decls[u]['at'] | refs.get(u, set()) if (r, off) in edits and (r, off) not in win_tokens})
    lines += ['', f'== renamed from the web / Android parse only (their #ifdef code; check by eye) ({len(other)})'] +              [f'{o} -> {n}  {r}' for o, n, r in other]
    lines += ['', f'== template references renamed by name: {dep_done}; left as they are ({len(dep_left)})'] + \
             [f'{n}: {v}' for n, v in sorted(dep_left.items())]
    lines += ['', f'== project files no build parses ({len(unseen)})'] + unseen
    lines += ['', f'== parse problems ({len(errors)})'] + errors
    (OUT / 'report.txt').write_text('\n'.join(lines), encoding='utf-8')
    print('\n'.join(lines[:3]))
    print(f'{OUT / "report.txt"}  ({time.time() - t0:.0f} s)')


def apply(a):
    data = json.loads((OUT / 'plan.json').read_text(encoding='utf-8'))
    by_file = defaultdict(list)
    for r, off, old, new in data['edits']:
        by_file[r].append((off, old, new))
    bad = 0
    for r, items in sorted(by_file.items()):
        p = ROOT / r
        b = bytearray(p.read_bytes())
        for off, old, new in sorted(items, reverse=True):
            ob = old.encode()
            if bytes(b[off:off + len(ob)]) != ob:
                bad += 1
                print(f'  skipped {r}:{off}: expected {old}')
                continue
            b[off:off + len(ob)] = new.encode()
        p.write_bytes(bytes(b))
    print(f'applied {len(data["edits"]) - bad} edits in {len(by_file)} files ({bad} skipped)')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('what', choices=['plan', 'apply'])
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 4) - 2))
    ap.add_argument('--only', default='', help='only the translation units whose path contains this')
    a = ap.parse_args()
    plan(a) if a.what == 'plan' else apply(a)


if __name__ == '__main__':
    main()
