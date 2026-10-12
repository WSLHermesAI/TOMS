#!/usr/bin/env python3
"""test_select.py -- run only the tests the current changes can break (tools/test.cmd calls it; docs/09_TESTS.md).

    tools\\test.cmd                       the tests for what changed (uncommitted changes, untracked files included)
    tools\\test.cmd all                   every test, web included
    tools\\test.cmd --since main          the tests for what changed since a commit / branch
    tools\\test.cmd --list                only print which tests would run, and why
    tools\\test.cmd -L unit               any ctest filter (-L -LE -R -E -I) is passed through as is, no selection

Every area has its own tests, and an area that did not change is not tested (the owner, 2026-10-12: "if glTF did not
change, skip the glTF tests; if the game did not change, skip all game tests"):

  area            its files                                              its tests
  game            src/core/game, src/game, assets/ (data, UI, sprites)   the game's unit tests (src/core/game/**_test)
                                                                         + the game screenshots on D3D11, narrowed by
                                                                         the file's name (menu, battle, store, ...)
  glTF            gltf code, gltf_viewer, mesh shaders, tests/gltf       unit.gltf_* + the glTF screenshots
  particles       particle_fx, the particle editor, fx files, fx shaders particle checks, unit test, smoke.fx*
  node anims      anim_clip / anim_player, the anim editor, .anim files  anim checks, unit test, smoke.anim
  atlases         the atlas tool, atlas files                            atlas checks (+ game screenshots for sprites)
  engine core     the rest of src/core/engine (conditions, nodes, ...)   the engine's unit tests + the game area
  renderer        src/engine: shared files                               every screenshot, every backend
                  src/engine: sprite / UI / ImGui files and shaders      the game screenshots, every backend
  web             web/, the web test                                     the web tests
  tests           a *_test.cpp, a tests/golden/x.png, a line of          exactly those tests
                  tests/CMakeLists.txt
  global          the root CMakeLists.txt, presets, cmake/, external/   every desktop test
  nothing         docs, *.md, the HTML editors, Android, this script

A file no rule knows runs nothing and is listed as "not mapped": add a rule to RULES for it.
"""
import fnmatch
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ---- test groups: predicates over a test {name, labels, golden, src} ----
def is_web(t):        return 'web' in t['labels']
def is_smoke(t):      return 'smoke' in t['labels']
def is_gltf(t):       return 'gltf' in t['labels']
def is_game_smoke(t): return is_smoke(t) and not is_gltf(t)
BACKEND_VARIANT = re.compile(r'_(d3d12|vulkan|opengl|cpu|compute|gpu)$')
def is_game_smoke_d3d11(t): return is_game_smoke(t) and not BACKEND_VARIANT.search(t['name'])
def named(*patterns): return lambda t: any(fnmatch.fnmatch(t['name'], p) for p in patterns)
def any_of(*preds):   return lambda t: any(p(t) for p in preds)
def unit_in(prefix):  return lambda t: t['name'].startswith('unit.') and t['src'].startswith(prefix)
DESKTOP = lambda t: not is_web(t)

GAME_UNIT = unit_in('game/')
ENGINE_SPECIFIC = re.compile(r'^unit\.(gltf_|anim_clip|particle_fx)')
ENGINE_UNIT = lambda t: unit_in('engine/')(t) and not ENGINE_SPECIFIC.match(t['name'])
GLTF = any_of(named('unit.gltf_*'), is_gltf)
PARTICLES = named('unit.particle_fx_test', 'particle.*', 'smoke.fx*')
ANIMS = named('unit.anim_clip_test', 'anim.*', 'smoke.anim')
ATLAS = named('atlas.*', 'unit.atlas_sprites_test')

# The game's screenshots narrowed by the changed file's name; a game file matching none runs every game screenshot.
GAME_SMOKE_BY_NAME = [
    (re.compile(r'player|saveload|inventory|equipment|items?\.json|stats\.json'), named('smoke.player_*')),
    (re.compile(r'battle|combat'), named('smoke.battle')),
    (re.compile(r'store'), named('smoke.store')),
    (re.compile(r'dialog'), named('smoke.dialogue', 'smoke.stairs_unlock')),
    (re.compile(r'title|art_style'), named('smoke.title')),
    (re.compile(r'stair|floor|stage|scene_draw|camera'), named('smoke.stage', 'smoke.stairs_*')),
    (re.compile(r'hud'), named('smoke.stage', 'smoke.player_*')),
]

# (glob patterns, tests, why); the first rule a file matches decides. Paths are repo-relative with '/'.
RULES = [
    # files other rules would take for the wrong area
    (['docs/examples/fx_*'], named('particle.check_recipes'), 'particle recipes'),
    (['docs/examples/anim_*'], named('anim.check_recipes'), 'anim recipes'),
    (['tests/smoke/anim_preview.anim'], named('anim.check_preview'), 'the anim preview'),
    (['tools/test_select.py', 'tools/test.cmd'], None, 'the test runner itself'),
    (['tools/web_editors/floor_index.js'], named('unit.floor_index_test'), 'the event index the HTML editors write'),
    # nothing
    (['docs/*', '*.md', 'tools/web_editors/*', 'android/*', '.claude/*', '.gitignore', '.gitattributes', 'AIGamestyle*',
      'tools/make_ui_font.py'],
     None, 'docs / HTML editors / Android: no test'),
    # global
    (['CMakeLists.txt', 'CMakePresets.json', 'cmake/*', 'external/*', 'tools/build.cmd'], DESKTOP,
     'global build setup: every desktop test'),
    # the tests themselves
    (['tests/CMakeLists.txt'], 'TESTS_CMAKE', 'the test list: the tests on the changed lines'),
    (['tests/smoke/*'], is_smoke, 'the screenshot runner: every screenshot'),
    (['*_test.cpp'], 'SELF', 'a unit test itself'),
    (['tests/golden/*.png'], 'GOLDEN', 'a reference image: the screenshot tests that use it'),
    # web
    (['web/*', 'tools/web_smoke_test.mjs', 'tools/build_web.cmd', 'tools/serve_web*'], is_web, 'the web page: web tests'),
    # glTF
    (['src/core/engine/gltf*', 'src/engine/src/gltf*', 'src/engine/shaders/*mesh*', 'src/engine/shaders/fs_shadow.sc',
      'tools/gltf_viewer/*', 'tests/gltf/*'], GLTF, 'glTF: its unit tests and screenshots'),
    # particles, node animations, atlases, the Qt editors' shared code
    (['src/core/engine/particle*', 'tools/particle/*', 'src/engine/shaders/cs_fx_*'], PARTICLES,
     'particles: checks, unit test, fx screenshots'),
    (['assets/media/particle*', 'assets/media/fx/*'], any_of(PARTICLES), 'particle files'),
    (['src/core/engine/anim*', 'tools/anim/*'], ANIMS, 'node animations: checks, unit test, anim screenshot'),
    (['assets/media/anim/*'], any_of(ANIMS, is_game_smoke_d3d11), '.anim files: anim checks + game screenshots'),
    (['tools/atlas/*', 'src/core/engine/atlas*'], ATLAS, 'the atlas tool: atlas checks'),
    (['assets/media/atlas/*'], any_of(ATLAS, is_game_smoke_d3d11), 'atlas files: atlas checks + game screenshots'),
    (['tools/studio_common/*'], named('atlas.core', 'anim.*', 'particle.*'), 'the Qt editors: their checks'),
    # the renderer
    (['src/engine/src/bgfx_*', 'src/engine/src/embedded_shaders*', 'src/engine/src/shared_*', 'src/engine/CMakeLists.txt',
      'src/engine/shaders/varying.def.sc', 'src/engine/shaders/*.sh'],
     any_of(is_smoke, named('unit.gltf_*')), 'the shared renderer: every screenshot, every backend'),
    (['src/engine/*'], is_game_smoke, 'the 2D / UI renderer: the game screenshots, every backend'),
    # engine core, then the game
    (['src/core/engine/*'], 'ENGINE', 'engine core: the engine unit tests + the game'),
    (['src/core/game/*', 'src/game/*', 'assets/*', 'src/core/CMakeLists.txt'], 'GAME',
     'the game: its unit tests + its screenshots (D3D11)'),
]


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True, encoding='utf-8').stdout


def changed_files(since):
    files = set(l for l in git('diff', '--name-only', since or 'HEAD').splitlines() if l)
    for line in git('status', '--porcelain', '--untracked-files=all').splitlines():
        if line.startswith('??'):
            files.add(line[3:].strip().strip('"'))
    return sorted(f.replace('\\', '/') for f in files)


def unit_sources():
    """unit test name -> its source path under src/core (from tests/CMakeLists.txt's TOMS_UNIT_TESTS)."""
    text = open(os.path.join(ROOT, 'tests', 'CMakeLists.txt'), encoding='utf-8').read()
    m = re.search(r'set\(TOMS_UNIT_TESTS(.*?)\)', text, re.S)
    out = {}
    for path in (m.group(1).split() if m else []):
        out[path.split('/')[-1]] = path
    return out


def list_tests(ctest, preset):
    out = subprocess.run([ctest, '--preset', preset, '-N', '--show-only=json-v1'], cwd=ROOT,
                         capture_output=True, text=True, encoding='utf-8').stdout
    data = json.loads(out[out.index('{'):])
    srcs = unit_sources()
    tests = []
    for t in data.get('tests', []):
        props = {p['name']: p['value'] for p in t.get('properties', [])}
        golden = next((c.split('=', 1)[1].replace('\\', '/').split('/')[-1] for c in t.get('command', [])
                       if c.startswith('-DGOLDEN=')), '')
        short = t['name'].split('.', 1)[-1]
        tests.append({'name': t['name'], 'labels': props.get('LABELS', []), 'golden': golden,
                      'src': srcs.get(short, '') if t['name'].startswith('unit.') else ''})
    return tests


def tests_cmake_changes(since, tests):
    """The tests tests/CMakeLists.txt's changed lines name, directly or through a variable they set."""
    diff = git('diff', '-U0', since or 'HEAD', '--', 'tests/CMakeLists.txt')
    lines = [l[1:] for l in diff.splitlines() if l[:1] in '+-' and not l.startswith(('+++', '---'))]
    lines = [l for l in lines if l.strip() and not l.strip().startswith('#')]
    text = open(os.path.join(ROOT, 'tests', 'CMakeLists.txt'), encoding='utf-8').read()
    picked = set()
    for l in lines:
        for t in tests:
            short = t['name'].split('.', 1)[-1]
            if re.search(r'(?<![\w])' + re.escape(short) + r'(?![\w])', l):
                picked.add(t['name'])
        for var in re.findall(r'set\((\w+)', l):   # a variable the tests use, e.g. ${_pm_give}
            for line in text.splitlines():
                if '${' + var + '}' in line:
                    m = re.match(r'\s*toms_\w+\((\w+)', line)
                    if m:
                        picked.update(t['name'] for t in tests if t['name'].split('.', 1)[-1] == m.group(1))
    return picked


def select(files, tests, since):
    chosen, unmapped = {}, []

    def take(pred, why):
        for t in tests:
            if pred(t):
                chosen.setdefault(t['name'], why)

    def game(f, why):
        take(GAME_UNIT, why)
        base = os.path.basename(f).lower()
        narrowed = [p for rx, p in GAME_SMOKE_BY_NAME if rx.search(base)]
        take(any_of(*narrowed) if narrowed else is_game_smoke_d3d11, why)

    for f in files:
        for patterns, what, why in RULES:
            if not any(fnmatch.fnmatch(f, p) for p in patterns):
                continue
            why = f + ': ' + why
            if what is None:
                pass
            elif what == 'SELF':
                take(named('unit.' + os.path.splitext(os.path.basename(f))[0]), why)
            elif what == 'GOLDEN':
                take(lambda t, g=os.path.basename(f): t['golden'] == g, why)
            elif what == 'TESTS_CMAKE':
                picked = tests_cmake_changes(since, tests)
                take(lambda t: t['name'] in picked, why)
            elif what == 'ENGINE':
                take(ENGINE_UNIT, why)
                game(f, why)
            elif what == 'GAME':
                game(f, why)
            else:
                take(what, why)
            break
        else:
            unmapped.append(f)
    return chosen, unmapped


def main():
    # tools/test.cmd passes --ctest <exe> --preset <name>, then the user's words: [all|changed] [--since REF] [--list]
    # and any ctest options, kept in order.
    argv = sys.argv[1:]
    opts, rest, i = {'mode': None, 'since': None, 'list': False, 'ctest': None, 'preset': None}, [], 0
    while i < len(argv):
        a = argv[i]
        if a in ('--ctest', '--preset', '--since') and i + 1 < len(argv):
            opts[a[2:]] = argv[i + 1]
            i += 2
            continue
        if a == '--list':
            opts['list'] = True
        elif a in ('all', 'changed') and opts['mode'] is None and not rest:
            opts['mode'] = a
        else:
            rest.append(a)
        i += 1
    base = [opts['ctest'], '--preset', opts['preset'], '-j', '4']
    if opts['mode'] == 'all' or any(a in rest for a in ('-L', '-LE', '-R', '-E', '-I', '--label-regex', '--tests-regex')):
        print('[toms] running %s' % ('every test' if opts['mode'] == 'all' else 'the given ctest filter'))
        return subprocess.call(base + rest, cwd=ROOT)

    files = changed_files(opts['since'])
    tests = list_tests(opts['ctest'], opts['preset'])
    chosen, unmapped = select(files, tests, opts['since'])
    print('[toms] %d changed file(s)%s; %d of %d tests selected (tools\\test.cmd all runs every test)'
          % (len(files), ' since ' + opts['since'] if opts['since'] else '', len(chosen), len(tests)))
    if unmapped:
        print('[toms] NOT MAPPED -- no test ran for these; add a rule to RULES in tools/test_select.py:')
        for f in unmapped:
            print('         ' + f)
    if opts['list']:
        for t in tests:
            if t['name'] in chosen:
                print('  %-34s %s' % (t['name'], chosen[t['name']]))
        return 0
    if not chosen:
        print('[toms] nothing to test for these changes')
        return 0
    regex = '^(' + '|'.join(re.escape(n) for n in chosen) + ')$'
    return subprocess.call(base + ['-R', regex] + rest, cwd=ROOT)


if __name__ == '__main__':
    sys.exit(main())
