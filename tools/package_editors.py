#!/usr/bin/env python3
"""package_editors.py -- the editor release in Editor/ (committed; the programs and DLLs go through Git LFS).

    tools\\package_editors.cmd                 build windows-release, then fill Editor/bin
    python tools/package_editors.py --no-build fill Editor/bin from the last build
    python tools/package_editors.py --check    only report what Editor/bin lacks or has extra (exit 1 if so)

Editor/bin gets the editors and the two command-line tools, the Qt 6 DLLs and plugins they load, the shader
compiler DLLs bgfx loads at run time (Direct3D 12), and the Microsoft C++ runtime (app-local, so a PC without
the Visual C++ Redistributable runs them too). Editor/BUILD_INFO.md records the commit, the date and every file.
The launchers (Editor/*.cmd) and the documents (Editor/docs) are written by hand and kept as they are.
"""
import datetime
import glob
import hashlib
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRESET = 'windows-release'
SRC = os.path.join(ROOT, 'Build', PRESET, 'bin')
DST = os.path.join(ROOT, 'Editor', 'bin')

PROGRAMS = ['atlas_editor.exe', 'anim_editor.exe', 'particle_editor.exe', 'font_editor.exe', 'gltf_viewer.exe',
            'toms_editor.exe', 'stage_event_editor.exe', 'ui_editor.exe', 'battle_editor.exe', 'atlaspack.exe', 'fontpack.exe']
DLLS = ['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'Qt6Svg.dll',   # Svg: the SVG icon engine plugin
        'Qt6Network.dll',                                              # stage_event_editor: is the HTML editors' server up
        'dxcompiler.dll', 'dxil.dll']                                  # bgfx's Direct3D 12 shader compiler
# Qt plugin folders (what windeployqt put next to the editors, minus networking the editors never use)
PLUGINS = ['platforms', 'styles', 'imageformats', 'iconengines', 'generic']


def crt_dir():
    """The newest Microsoft.VC14x.CRT (x64) of the installed Visual Studio."""
    vswhere = os.path.join(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'),
                           'Microsoft Visual Studio', 'Installer', 'vswhere.exe')
    roots = []
    if os.path.exists(vswhere):
        out = subprocess.run([vswhere, '-latest', '-products', '*', '-property', 'installationPath'],
                             capture_output=True, text=True).stdout.strip()
        if out:
            roots.append(out)
    best = None
    for r in roots:
        for d in glob.glob(os.path.join(r, 'VC', 'Redist', 'MSVC', '*', 'x64', 'Microsoft.VC14*.CRT')):
            ver = os.path.basename(os.path.dirname(os.path.dirname(d)))
            key = tuple(int(p) for p in ver.split('.') if p.isdigit())
            if key and (best is None or key > best[0]):
                best = (key, d)
    return best[1] if best else None


def wanted():
    """published path (relative to Editor/bin) -> source file."""
    files = {}
    for name in PROGRAMS + DLLS:
        files[name] = os.path.join(SRC, name)
    for d in PLUGINS:
        for f in sorted(glob.glob(os.path.join(SRC, d, '*.dll'))):
            files[d + '/' + os.path.basename(f)] = f
    crt = crt_dir()
    if crt:
        for f in sorted(glob.glob(os.path.join(crt, '*.dll'))):
            files[os.path.basename(f)] = f
    return files, crt


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()[:12]


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()


def main():
    args = sys.argv[1:]
    if '--no-build' not in args and '--check' not in args:
        if subprocess.call(['cmd', '/c', os.path.join(ROOT, 'tools', 'build.cmd'), PRESET]) != 0:
            print('[package] the build failed; Editor/ is unchanged')
            return 2
    files, crt = wanted()
    missing = [k for k, v in files.items() if not os.path.exists(v)]
    if missing:
        print('[package] missing in Build/%s/bin (build first?): %s' % (PRESET, ', '.join(missing)))
        return 2
    if not crt:
        print('[package] no Microsoft.VC14x.CRT found in Visual Studio (VC/Redist/MSVC/*/x64)')
        return 2
    have = set()
    for dirpath, _, names in os.walk(DST):
        for n in names:
            have.add(os.path.relpath(os.path.join(dirpath, n), DST).replace('\\', '/'))
    if '--check' in args:
        extra, lack = sorted(have - set(files)), sorted(set(files) - have)
        stale = sorted(k for k in set(files) & have if sha(files[k]) != sha(os.path.join(DST, k)))
        for k in lack: print('  missing:', k)
        for k in extra: print('  extra:  ', k)
        for k in stale: print('  stale:  ', k)
        print('[package] Editor/bin is', 'up to date' if not (extra or lack or stale) else 'out of date')
        return 1 if (extra or lack or stale) else 0
    # copy what changed, remove what is no longer published
    for k in sorted(have - set(files)):
        os.remove(os.path.join(DST, k))
    for k, src in sorted(files.items()):
        dst = os.path.join(DST, k)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if not os.path.exists(dst) or sha(src) != sha(dst):
            shutil.copy2(src, dst)
    for dirpath, dirs, names in os.walk(DST, topdown=False):
        if not dirs and not names and dirpath != DST:
            os.rmdir(dirpath)
    # BUILD_INFO.md
    head = git('rev-parse', '--short', 'HEAD')
    dirty = git('status', '--porcelain', '--', 'src', 'tools', 'CMakeLists.txt', 'cmake')
    total = sum(os.path.getsize(os.path.join(DST, k)) for k in files)
    lines = ['# Editor build', '',
             'Written by `tools/package_editors.py`; do not edit.', '',
             '| | |', '|---|---|',
             '| Date | %s |' % datetime.datetime.now().strftime('%Y-%m-%d %H:%M'),
             '| Source | commit `%s`%s |' % (head, ' plus uncommitted changes' if dirty else ''),
             '| Build | `%s` (MSVC, Qt 6, x64) |' % PRESET,
             '| C++ runtime | `%s` |' % os.path.relpath(crt, os.path.dirname(os.path.dirname(os.path.dirname(crt)))).replace('\\', '/'),
             '| Size | %.1f MB in %d files |' % (total / 1e6, len(files)), '',
             '| File | Size | SHA-256 (12) |', '|---|---:|---|']
    for k in sorted(files):
        p = os.path.join(DST, k)
        lines.append('| `bin/%s` | %s | `%s` |' % (k, '{:,}'.format(os.path.getsize(p)), sha(p)))
    with open(os.path.join(ROOT, 'Editor', 'BUILD_INFO.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    print('[package] Editor/bin: %d files, %.1f MB (commit %s%s)' % (len(files), total / 1e6, head, ', dirty' if dirty else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main())
