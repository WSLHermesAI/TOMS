#!/usr/bin/env python3
"""test_select.py -- run only the tests the current changes can break (tools/test.cmd calls it; docs/project/08_TESTS.md).

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
  picture fonts   tools/font, tests/font, bitmap_font.*                  font.tool, font.editor_selftest
  resources       resource* / render_object* in src/core/engine          engine unit tests + anim / fx checks + the game area
  engine core     the rest of src/core/engine (conditions, nodes, ...)   the engine's unit tests + the game area
  renderer        src/engine: shared files                               every screenshot, every backend
                  src/engine: sprite / UI / ImGui files and shaders      the game screenshots, every backend
  web             web/, the web test                                     the web tests
  tests           a *_test.cpp, a line of tests/CMakeLists.txt          exactly those tests
                  a folder under tests/: tests/test_folders.json says    that folder's tests; a folder not listed there
                  which ones are test data and what they feed            runs nothing (it is not test data)
  global          the root CMakeLists.txt, presets, cmake/, external/   every desktop test
  nothing         docs, *.md, the HTML editors, Android, this script

A file no rule knows runs nothing and is listed as "not mapped": add a rule to RULES for it.

Tests switched off in tests/test_switches.json (edit it with tools\test_switches.cmd: a tree with a checkbox per test)
never run, changed-only or `all`; a ctest filter given by hand (-R, -L ...) runs as given.
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
def is_scenario(t):    return 'scenario' in t['labels']   # tests/scenarios/<name>/ (docs/gameplay/08_TEST_SCENARIOS.md)
def named(*patterns): return lambda t: any(fnmatch.fnmatch(t['name'], p) for p in patterns)
def any_of(*preds):   return lambda t: any(p(t) for p in preds)
def unit_in(prefix):  return lambda t: t['name'].startswith('unit.') and t['src'].startswith(prefix)
DESKTOP = lambda t: not is_web(t)

GAME_UNIT = unit_in('game/')
ENGINE_SPECIFIC = re.compile(r'^unit\.(gltf_|anim_clip|anim_skin|anim_character|anim_lattice|sprite_mesh|particle_fx)')
ENGINE_UNIT = lambda t: unit_in('engine/')(t) and not ENGINE_SPECIFIC.match(t['name'])
GLTF = any_of(named('unit.gltf_*'), is_gltf)
PARTICLES = named('unit.particle_fx_test', 'particle.*', 'smoke.fx*')
ANIMS = named('unit.anim_clip_test', 'unit.anim_skin_test', 'unit.anim_character_test', 'unit.anim_lattice_test', 'anim.*',
              'unit.anim_fx_test', 'smoke.anim', 'smoke.anim_skin', 'smoke.anim_deform', 'smoke.anim_fx')
# sprite meshes (docs/engine/16_MESH_ANIMATION.md): the mesh file and cutter, the skinning that bends them, `atlaspack meshes`
MESHES = named('unit.sprite_mesh_test', 'unit.anim_skin_test', 'unit.anim_lattice_test', 'smoke.anim_skin',
               'smoke.anim_deform', 'anim.check_skin', 'anim.check_skin_meshes', 'anim.meshes_up_to_date')
ATLAS = named('atlas.*', 'unit.atlas_sprites_test')
FONTS = named('font.*')

# The game's screenshots narrowed by the changed file's name; a game file matching none runs every game screenshot.
# The scenario tests (tests/scenarios/) are narrowed the same way: each scene's scenarios with its screenshots.
GAME_SMOKE_BY_NAME = [
    (re.compile(r'scenario'), is_scenario),
    (re.compile(r'player|saveload|inventory|equipment|items?\.json|stats\.json'),
     named('smoke.player_*', 'scenario.menu_*', 'scenario.save_load')),
    (re.compile(r'battle|combat|enemies\.json|looks\.json|moves\.json'), named('smoke.battle', 'scenario.battle_*')),
    (re.compile(r'store'), named('smoke.store', 'scenario.store_*')),
    (re.compile(r'dialog'), named('smoke.dialogue', 'smoke.stairs_unlock', 'scenario.dialogue_*', 'scenario.talk_*')),
    (re.compile(r'talk|npc'), named('smoke.talk_*', 'scenario.talk_*')),
    (re.compile(r'title|art_style'), named('smoke.title', 'scenario.title')),
    (re.compile(r'stair|floor|stage|scene_draw|camera'),
     named('smoke.stage', 'smoke.stairs_*', 'scenario.stage_*', 'scenario.stairs_*', 'scenario.notice_*')),
    (re.compile(r'ending'), named('scenario.ending')),
    (re.compile(r'hub'), named('scenario.hub_*')),
    (re.compile(r'hud'), named('smoke.stage', 'smoke.player_*')),
]

# tests/test_folders.json: the folders under tests/ that are test data, and the tests each one feeds. A test entry is a
# ctest name (wildcards allowed), 'label:<label>', or '@golden' (the screenshot tests using the changed image).
TEST_FOLDERS_FILE = 'tests/test_folders.json'


def test_folders():
    try:
        with open(os.path.join(ROOT, TEST_FOLDERS_FILE), encoding='utf-8') as f:
            j = json.load(f)
    except (OSError, ValueError) as e:
        print('[toms] %s unreadable (%s): every folder under tests/ runs nothing' % (TEST_FOLDERS_FILE, e))
        return {}, {}
    return j.get('folders', {}), j.get('files', {})


TEST_SWITCHES_FILE = 'tests/test_switches.json'


def switched_off():
    """The tests the owner switched off (tools/test_switches.cmd writes the file)."""
    try:
        with open(os.path.join(ROOT, TEST_SWITCHES_FILE), encoding='utf-8-sig') as f:
            return set(json.load(f).get('off', []))
    except FileNotFoundError:
        return set()
    except (OSError, ValueError) as e:
        print('[toms] %s unreadable (%s): no test is switched off' % (TEST_SWITCHES_FILE, e))
        return set()


def folder_pred(entries, f):
    preds = []
    for e in entries:
        if e == '@golden':
            preds.append(lambda t, g=os.path.basename(f): t['golden'] == g)
        elif e.startswith('label:'):
            preds.append(lambda t, l=e[6:]: l in t['labels'])
        else:
            preds.append(named(e))
    return any_of(*preds)


# (glob patterns, tests, why); the first rule a file matches decides. Paths are repo-relative with '/'.
RULES = [
    # files other rules would take for the wrong area
    (['docs/examples/fx_*'], named('particle.check_recipes'), 'particle recipes'),
    (['docs/examples/anim_*'], named('anim.check_recipes'), 'anim recipes'),
    (['docs/examples/tutorial/*', 'docs/engine/17_SKINNING_TUTORIAL.md', 'docs/engine/zh_TW/17_SKINNING_TUTORIAL.md'],
     named('anim.check_tutorial_*', 'anim.editor_selftest', 'anim.meshes_up_to_date'),
     'the skinning tutorial and its golem files (the editor opens them from Help)'),
    # picture fonts: toms_core builds the runtime, but no game screen draws with it yet, so the font tool's
    # tests (which test the runtime too); add the game's screenshots here once a screen uses FontText
    (['tools/font/*', 'src/core/engine/bitmap_font*'], FONTS,
     'picture fonts: the font tool tests'),
    (['tools/test_select.py', 'tools/test.cmd', 'tests/test_folders.json', 'tests/test_switches.json',
      'tools/test_switches.*'], None, 'the test runner itself'),
    # the editor release: copies of what the build made, launchers and docs (run `tools\test.cmd all` before a release)
    (['Editor/*', 'tools/package_editors.*'], None, 'the packaged editors (Editor/): no test'),
    (['tools/web_editors/floor_index.js'], named('unit.floor_index_test', 'unit.stage_core_test'),
     'the event index the HTML editors write (the native editor builds the same bytes)'),
    (['tools/stage_event_editor/*'], named('unit.stage_core_test', 'stage_editor.*'),
     'the stage / event editor app: its rules test and its window self test'),
    (['tools/make_ui_font.py', 'assets/media/fonts/*'], named('data.ui_font_covers_text'), 'the UI font subset: it covers the text'),
    (['tools/make_combo_sfx.py', 'tools/make_battle_sfx.py'], None,
     'writes sounds (assets/media/sfx: combo_ready, the battle_* moments of docs/gameplay/15): no automated test, listen to them'),
    # nothing
    (['docs/*', '*.md', 'tools/web_editors/*', 'android/*', '.claude/*', '.gitignore', '.gitattributes', 'AIGamestyle*', 'launch.vs.json'],
     None, 'docs / HTML editors / Android: no test'),
    (['tools/release_*.cmd', 'tools/publish_web.cmd', 'setup_new_pc.bat', 'TOMS_Launcher.bat',
      'tools/launcher*', 'tools/build_android.cmd', 'tools/run_android.cmd',
      'tools/check_env.ps1', 'tools/check_git_ssh.*', 'tools/package.ps1', 'tools/publish_pages.ps1'], None,
     'setup, web publish, Android and packaging scripts: no automated test (run them by hand)'),
    (['tools/build_gltf_web.cmd', 'tools/build_atlas_web.cmd'], None,
     'the web glTF viewer / web atlas tool builds: no automated test (build and open them by hand)'),
    # global
    (['CMakeLists.txt', 'CMakePresets.json', 'cmake/*', 'external/*', 'tools/build.cmd'], DESKTOP,
     'global build setup: every desktop test'),
    # the tests themselves
    (['tests/CMakeLists.txt'], 'TESTS_CMAKE', 'the test list: the tests on the changed lines'),
    (['*_test.cpp'], 'SELF', 'a unit test itself'),
    # web
    (['web/*', 'tools/web_smoke_test.mjs', 'tools/build_web.cmd', 'tools/serve_web*'], is_web, 'the web page: web tests'),
    # glTF
    (['src/core/engine/gltf*', 'src/engine/src/gltf*', 'src/engine/shaders/*mesh*', 'src/engine/shaders/fs_shadow.sc',
      'tools/gltf_viewer/*'], GLTF, 'glTF: its unit tests and screenshots'),
    # particles, node animations, atlases, the Qt editors' shared code
    (['src/core/engine/particle*', 'tools/particle/*', 'src/engine/shaders/cs_fx_*'], PARTICLES,
     'particles: checks, unit test, fx screenshots'),
    (['assets/media/fx/battle.particle', 'tools/make_battle_fx.py'], named('particle.check_battle', 'unit.weapon_play_test'),
     "the battle's weapon effects: their check + weapon_play.json's names in them"),
    (['assets/media/particle*', 'assets/media/fx/*'], any_of(PARTICLES), 'particle files'),
    (['src/core/engine/sprite_mesh*'], MESHES, 'sprite meshes: their unit tests, the skinned and deformed clips'),
    (['src/core/engine/anim*', 'tools/anim/*'], ANIMS, 'node animations: checks, unit test, anim screenshot'),
    (['*.mesh.json', '*.mesh.bin', 'tools/refresh_meshes.py'], MESHES,
     "an atlas's sprite meshes (written by the anim tools): the mesh tests"),
    (['assets/media/resources.json', 'tools/make_res_keys.py', 'src/core/game/core/res_keys.h'],
     any_of(named('unit.resource_test', 'unit.res_keys_test'), is_game_smoke_d3d11),
     'the resource manifest and its keys: its checks (every entry is there, res_keys.h up to date) + the game screenshots'),
    # the bow's clips: no game screen plays them yet, so only their check
    (['assets/media/anim/weapon_sword.anim', 'assets/media/anim/fighter_sword.anim', 'assets/media/anim/weapon_staff.anim',
      'assets/media/anim/caster_staff.anim', 'tools/make_weapon_anims.py'],
     named('anim.check_weapon_*', 'anim.check_fighter_sword', 'anim.check_caster_staff', 'scenario.battle_sword', 'scenario.battle_staff'),
     'the sword and staff clips (make_weapon_anims.py writes them): their checks and battles'),
    (['tools/make_weapon_icons.py'], named('atlas.*', 'unit.atlas_sprites_test'), 'the weapon icons: the atlas checks'),
    (['assets/media/anim/monster_battle.anim', 'tools/make_monster_battle_anims.py'],
     named('anim.check_monster_battle', 'scenario.battle_*'),
     'the monsters on the battle screen (make_monster_battle_anims.py writes them): their check and the battles'),
    (['assets/media/anim/weapon_bow.anim', 'tools/make_bow_anim.py'], named('anim.check_bow', 'anim.check_archer', 'unit.anim_character_test'),
     'the bow clips (make_bow_anim.py writes them; the archer clips are built on them): their checks'),
    (['assets/media/anim/archer_bow.anim', 'tools/make_archer_anim.py', 'assets/data/character_fit.json'],
     named('anim.check_archer', 'unit.anim_character_test', 'anim.editor_selftest'),
     'the archer clips and the character nudges: their check, the fitting test, the editor preview'),
    (['assets/media/anim/brawler*.anim', 'tools/make_brawler_anim.py', 'tools/art/brawler_rig.json',
      'assets/media/atlas/brawler.*'],
     named('anim.check_brawler*', 'unit.anim_fx_test', 'smoke.anim_fx'),
     'the brawler showcase (make_brawler_anim.py writes it; docs/engine/18): its checks, the effects test and screenshot'),
    (['assets/media/anim/*', 'tools/make_idle_clips.py'], any_of(ANIMS, is_game_smoke_d3d11),
     '.anim files (make_idle_clips.py writes the idle ones): anim checks + game screenshots'),
    (['tools/atlas/*', 'src/core/engine/atlas*'], ATLAS, 'the atlas tool: atlas checks'),
    # the raw art (art_raw/, not shipped): a project change can change the cooked atlas; raw PNGs change nothing
    # the game sees until they are imported and cooked
    (['art_raw/atlas/*'], any_of(ATLAS, is_game_smoke_d3d11), 'atlas projects: atlas checks + game screenshots'),
    (['tools/art/*'], None, 'the AI reskin scripts (art_raw/ in, art_raw/ out): no test'),
    (['art_raw/*'], ATLAS, 'raw art (sprite PNGs): the atlas checks'),
    (['assets/media/atlas/*'], any_of(ATLAS, is_game_smoke_d3d11), 'atlas files: atlas checks + game screenshots'),
    (['docs/examples/ui_tutorial/*', 'docs/gameplay/10_UI_EDITOR.md', 'docs/gameplay/zh_TW/10_UI_EDITOR.md'], named('ui_editor.selftest', 'smoke.ui_editor'),
     "the UI editor's tutorial and its practice screen (the editor opens them from Help)"),
    (['src/editor/src/uied_*'], named('ui_editor.selftest', 'smoke.ui_editor'), 'the UI editor (ui_editor)'),
    (['src/editor/src/bed_*', 'tools/battle_editor/*'], named('battle_editor.selftest', 'smoke.battle_editor'),
     'the battle editor (battle_editor, docs/gameplay/13) and its test setups'),
    (['src/editor/src/bgfx_viewport*'],
     named('ui_editor.selftest', 'smoke.ui_editor', 'battle_editor.selftest', 'smoke.battle_editor'),
     'the game viewport toms_editor, ui_editor and battle_editor share'),
    (['src/editor/*'], None, 'toms_editor (Qt + the game viewport): no automated test; the desktop build compiles it'),
    (['tools/studio_common/*'], named('atlas.core', 'anim.*', 'particle.*', 'font.*', 'studio.*', 'stage_editor.*'),
     'the Qt editors: their checks'),
    # the renderer
    (['src/engine/src/bgfx_*', 'src/engine/src/embedded_shaders*', 'src/engine/src/shared_*', 'src/engine/CMakeLists.txt',
      'src/engine/shaders/varying.def.sc', 'src/engine/shaders/*.sh'],
     any_of(is_smoke, named('unit.gltf_*')), 'the shared renderer: every screenshot, every backend'),
    (['src/engine/*'], is_game_smoke, 'the 2D / UI renderer: the game screenshots, every backend'),
    # engine core, then the game
    (['src/core/engine/resource*', 'src/core/engine/render_object*'], 'ENGINE_RES',
     'the resource cache and render objects: engine unit tests + the clip / effect previews + the game'),
    (['src/core/engine/*'], 'ENGINE', 'engine core: the engine unit tests + the game'),
    # tools/add_stage_uids.py writes the stage files' object ids (docs/gameplay/05_SAVE_SYSTEM.md)
    (['src/core/game/*', 'src/game/*', 'assets/*', 'src/core/CMakeLists.txt', 'tools/add_stage_uids.py',
      'tools/redesign_floors.py'], 'GAME',
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
    folders, folder_files = test_folders()
    not_test_data = []   # folders under tests/ that test_folders.json does not list

    def take(pred, why):
        for t in tests:
            if pred(t):
                chosen.setdefault(t['name'], why)

    def game(f, why):
        take(GAME_UNIT, why)
        if f.startswith(('assets/data/', 'assets/media/ui/', 'assets/media/styles/')):   # text: the UI font must have it
            take(named('data.ui_font_covers_text'), why)
        if f.startswith('assets/data/'):   # the native stage editor reads every floor, type and event (and the index)
            take(named('unit.stage_core_test'), why)
        if f in ('assets/data/enemies.json', 'assets/data/weapon_play.json', 'assets/data/stats.json'):
            take(named('battle_editor.selftest'), why)   # the battle editor writes these back in their own layout
        base = os.path.basename(f).lower()
        narrowed = [p for rx, p in GAME_SMOKE_BY_NAME if rx.search(base)]
        take(any_of(*narrowed) if narrowed else any_of(is_game_smoke_d3d11, is_scenario), why)

    names = {t['name'] for t in tests}
    for f in files:
        # a scenario folder (tests/scenarios/<name>/...): just that scenario; the shared files and the runner: all of them
        if f.startswith('tests/scenarios/'):
            parts = f.split('/')
            if len(parts) > 3 and 'scenario.' + parts[2] in names:
                take(named('scenario.' + parts[2]), f + ': its scenario')
            else:
                take(is_scenario, f + ': shared by the scenarios (or their runner): every scenario')
            continue
        # a folder under tests/: test_folders.json decides (a file entry first, then its folder; unlisted -> nothing)
        if f.startswith('tests/') and f.count('/') >= 2:
            rel = f[len('tests/'):]
            entry = folder_files.get(rel) or folders.get(rel.split('/', 1)[0])
            if entry:
                take(folder_pred(entry.get('tests', []), f), f + ': ' + entry.get('why', TEST_FOLDERS_FILE))
            else:
                top = 'tests/' + rel.split('/', 1)[0] + '/'
                if top not in not_test_data:
                    not_test_data.append(top)
            continue
        for patterns, what, why in RULES:
            if not any(fnmatch.fnmatch(f, p) for p in patterns):
                continue
            why = f + ': ' + why
            if what is None:
                pass
            elif what == 'SELF':
                take(named('unit.' + os.path.splitext(os.path.basename(f))[0]), why)
            elif what == 'TESTS_CMAKE':
                picked = tests_cmake_changes(since, tests)
                take(lambda t: t['name'] in picked, why)
            elif what == 'ENGINE':
                take(ENGINE_UNIT, why)
                game(f, why)
            elif what == 'ENGINE_RES':   # the game, the --anim / --fx previews and the tools load through it
                take(ENGINE_UNIT, why)
                take(ANIMS, why)
                take(PARTICLES, why)
                game(f, why)
            elif what == 'GAME':
                game(f, why)
            else:
                take(what, why)
            break
        else:
            unmapped.append(f)
    return chosen, unmapped, not_test_data


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
    off = switched_off()
    if any(a in rest for a in ('-L', '-LE', '-R', '-E', '-I', '--label-regex', '--tests-regex')):
        print('[toms] running the given ctest filter (switches not applied)')
        return subprocess.call(base + rest, cwd=ROOT)
    if opts['mode'] == 'all':
        if off:
            print('[toms] running every test but %d switched off in %s' % (len(off), TEST_SWITCHES_FILE))
            rest = rest + ['-E', '^(' + '|'.join(re.escape(n) for n in sorted(off)) + ')$']
        else:
            print('[toms] running every test')
        return subprocess.call(base + rest, cwd=ROOT)

    files = changed_files(opts['since'])
    tests = list_tests(opts['ctest'], opts['preset'])
    chosen, unmapped, not_test_data = select(files, tests, opts['since'])
    skipped = sorted(n for n in chosen if n in off)
    for n in skipped:
        del chosen[n]
    print('[toms] %d changed file(s)%s; %d of %d tests selected (tools\\test.cmd all runs every test)'
          % (len(files), ' since ' + opts['since'] if opts['since'] else '', len(chosen), len(tests)))
    if skipped:
        print('[toms] %d selected test(s) switched off in %s: %s' % (len(skipped), TEST_SWITCHES_FILE, ' '.join(skipped)))
    if not_test_data:
        print('[toms] not test data (not listed in %s), no test: %s' % (TEST_FOLDERS_FILE, ' '.join(not_test_data)))
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
