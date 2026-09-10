#!/usr/bin/env bash
# build_web.sh — one-click build of the Tower of the Sorcerer browser builds.
#
# Builds BOTH Emscripten browser backends of the game and drops the runnable
# artifacts into web-gl/ (WebGL2) and web-gpu/ (WebGPU):
#   tower_vulkan_web.html / .js / .wasm / .data
#
# Usage:
#   ./build_web.sh            # build both WebGL2 and WebGPU
#   ./build_web.sh webgl      # build only WebGL2
#   ./build_web.sh webgpu     # build only WebGPU
#
# Requirements (no root needed):
#   - Emscripten SDK at $HOME/opt/emsdk (or set EMSDK env var)
#   - cmake on PATH (this script adds $HOME/opt/cmake/bin if present)
#   - assets/ and data/ at the repo root (preloaded into the .data bundle)
#
set -euo pipefail

# ---- locate repo root (script lives in repo root or ./tools) ----
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# ---- toolchain setup ----
# Emscripten
if [ -z "${EMSDK:-}" ]; then
  for p in "$HOME/opt/emsdk" "$HOME/emsdk" "/opt/emsdk"; do
    if [ -f "$p/emsdk_env.sh" ]; then EMSDK="$p"; break; fi
  done
fi
if [ -z "${EMSDK:-}" ] || [ ! -f "$EMSDK/emsdk_env.sh" ]; then
  echo "[build_web] ERROR: Emscripten SDK not found. Install it or set \$EMSDK." >&2
  exit 1
fi
# shellcheck disable=SC1091
source "$EMSDK/emsdk_env.sh"

# cmake: prefer a bundled no-root cmake if present
if [ -x "$HOME/opt/cmake/bin/cmake" ]; then
  export PATH="$HOME/opt/cmake/bin:$PATH"
fi
command -v cmake >/dev/null 2>&1 || { echo "[build_web] ERROR: cmake not found on PATH." >&2; exit 1; }
command -v emcmake >/dev/null 2>&1 || { echo "[build_web] ERROR: emcmake not found (EMSDK not activated?)." >&2; exit 1; }

JOBS="$(nproc 2>/dev/null || echo 4)"

build_one() {
  local backend="$1" outdir="$2" builddir="$3"
  echo "=================================================================="
  echo "[build_web] Building backend: $backend  ->  $outdir/"
  echo "=================================================================="
  rm -rf "$builddir"
  local cmake_args=(-S . -B "$builddir" -DWEB=ON)
  if [ "$backend" = "WebGPU" ]; then
    cmake_args+=(-DWEB_BACKEND=WebGPU)
  fi
  emcmake cmake "${cmake_args[@]}"
  cmake --build "$builddir" -j"$JOBS"

  # Copy the linked artifacts out (both share the filename toms_web.*)
  mkdir -p "$outdir"
  cp -f web/toms_web.html \
        web/toms_web.js  \
        web/toms_web.wasm \
        web/toms_web.data "$outdir/"
  echo "[build_web] $backend artifacts -> $outdir/:"
  ls -la "$outdir"

  # ---- cache-busting + real-error display: stamp the generated HTML ----
  # A normal reload then always fetches fresh .wasm/.data (no hard-refresh),
  # and the page shows the REAL error text instead of the generic
  # "Exception thrown, see JavaScript console" string.
  local VER="$(git rev-parse --short HEAD 2>/dev/null || echo dev)-$(date +%Y%m%d%H%M%S)"
  for html in "$outdir/toms_web.html" "web/toms_web.html"; do
    [ -f "$html" ] || continue
    python3 - "$html" "$VER" <<'PY'
import sys
html, ver = sys.argv[1], sys.argv[2]
s = open(html, encoding='utf-8').read()
# 1) define window.TOMS_VERSION early (in <head> so it exists before Module).
#    NOTE: we deliberately do NOT override Module.locateFile. Overriding it
#    (even to return the same path) breaks Emscripten's asset loader in some
#    browsers (the .data preload fetch hangs at "Downloading..."). The cache-bust
#    ?v= query is therefore skipped; a hard refresh still picks up new builds.
tag = '<script>window.TOMS_VERSION=%r;</script>' % ver
# Guard: swallow unhandled promise rejections during init so they don't propagate
# to Emscripten's run() promise and abort the wasm (the "Exception thrown" crash).
# We still surface them via window.onunhandledrejection (installed below) for diagnostics.
guard = '<script>window.addEventListener("unhandledrejection",function(e){e.preventDefault();e.stopPropagation();});</script>'
if 'window.TOMS_VERSION' not in s:
    s = s.replace('<head>', '<head>\n    '+tag+'\n    '+guard, 1)
# 2) replace the generic window.onerror with one that shows the REAL message+stack
OLD = """      window.onerror = window.onunhandledrejection = () => {
        // TODO: do not warn on ok events like simulating an infinite loop or exitStatus
        setStatus('Exception thrown, see JavaScript console');
        spinnerElement.style.display = 'none';
        setStatus = (text) => {
          if (text) console.error('[post-exception status] ' + text);
        };
      };"""
if OLD in s and 'showErr' not in s:
    NEW = """      // Errors go to the BROWSER CONSOLE only: the published page keeps no on-screen debug
      // banner/console (owner request). Open DevTools to read them.
      function showErr(msg){ console.error('[TOMS] ' + msg); }
      function dumpErr(e){
        var parts = [];
        try { parts.push('type=' + (e && e.constructor && e.constructor.name)); } catch(_) {}
        if (e && e.message) parts.push('message=' + e.message);
        var src = (e && e.reason !== undefined) ? e.reason : (e && e.error);
        if (src === undefined && e && e.reason === undefined) parts.push('reason=undefined');
        if (src) {
          try {
            var info = [];
            if (src.name) info.push('name=' + src.name);
            if (src.code) info.push('code=' + src.code);
            if (src.message) info.push('msg=' + src.message);
            info.push(src.stack ? src.stack : (src.message ? src.message : String(src)));
            parts.push('reason={' + info.join(' | ') + '}');
          } catch(_) { parts.push('reason=' + String(src)); }
        }
        if (e && e.filename) parts.push('at ' + e.filename + ':' + e.lineno + ':' + e.colno);
        var txt = parts.join(' ');
        try { document.title = 'ERR: ' + txt.slice(0, 200); } catch(_) {}
        return txt;
      }
      window.onerror = (m,s,l,c,err) => { showErr(dumpErr(err || {message:m,filename:s,lineno:l,colno:c})); spinnerElement.style.display='none'; setStatus=(t)=>{if(t)console.error('[post] '+t);}; };
      window.onunhandledrejection = (e) => { showErr(dumpErr(e)); spinnerElement.style.display='none'; setStatus=(t)=>{if(t)console.error('[post] '+t);}; };
      if (typeof Module !== 'undefined') Module.onAbort = function(what){ showErr('ABORT: ' + (what||'unknown')); };"""
    s = s.replace(OLD, NEW, 1)
open(html, 'w', encoding='utf-8').write(s)
print('stamped', html, 'with v='+ver, '| showErr:', 'showErr' in s)
PY
  done

  # ---- strip the stock Emscripten shell chrome from the generated game page ----------------
  # The generated HTML is Emscripten's default shell: an Emscripten logo, a spinner/status/
  # progress block, and an #output textarea (a debug console). None of that belongs in the
  # shipped game, so it is removed here, on every build.
  for html in "$outdir/toms_web.html" "web/toms_web.html"; do
    [ -f "$html" ] || continue
    python3 - "$html" <<'PY'
import re, sys
path = sys.argv[1]
s = open(path, encoding='utf-8').read()

s = re.sub(r'<title>.*?</title>', '<title>Tower of the Sorcerer</title>', s, count=1, flags=re.S | re.I)
# the shell's logo anchor: <a href="...emscripten..."><img id="emscripten_logo" base64…></a>
s = re.sub(r'<a[^>]*href="[^"]*emscripten[^"]*"[^>]*>.*?</a>', '', s, count=1, flags=re.S | re.I)
for pat in (r'<div[^>]*id="status"[^>]*>.*?</div>',
            r'<div[^>]*class="spinner"[^>]*>.*?</div>',
            r'<progress[^>]*id="progress"[^>]*>.*?</progress>',
            r'<div[^>]*id="controls"[^>]*>.*?</div>',
            r'<textarea[^>]*id="output"[^>]*>.*?</textarea>',
            # CSS blocks for elements that no longer exist
            r'#emscripten_logo\s*\{[^}]*\}',
            r'\.spinner\s*\{[^}]*\}'):
    s = re.sub(pat, '', s, flags=re.S | re.I)
# dark, chrome-free page instead of the shell's default styling
s = s.replace('body {\n  font-family: arial;', 'body {\n  background: #05050a; overflow: hidden;\n  font-family: arial;')
open(path, 'w', encoding='utf-8').write(s)
print('cleaned %s | logo=%s status=%s output=%s tomserr=%s'
      % (path, 'emscripten_logo' in s, 'id="status"' in s, 'id="output"' in s, 'tomserr' in s))
PY
  done

  # ---- version the artifact filenames -------------------------------------------------------
  # A deploy replaces toms_web.data/.wasm under the SAME names, so a returning visitor can end
  # up with a fresh page JS against a cached old .data (size/offset mismatch => black canvas).
  # Naming the artifacts per build makes the filename itself the cache buster. The plain-named
  # copies are kept as well so a page cached from an earlier deploy still loads.
  python3 - "$outdir" "$VER" <<'PY'
import os, shutil, sys
outdir, ver = sys.argv[1], sys.argv[2]
ren = 'toms_web.' + ver
js = open(os.path.join(outdir, 'toms_web.js'), encoding='utf-8').read()
# only these two are fetch URLs; the embedded preload keys ("datafile_/…/toms_web.data") must keep
# their original names because they identify packages inside the .data file itself.
new = js.replace("'toms_web.wasm'", "'%s.wasm'" % ren).replace("'toms_web.data'", "'%s.data'" % ren)
open(os.path.join(outdir, ren + '.js'), 'w', encoding='utf-8').write(new)
for ext in ('wasm', 'data'):
    shutil.copy2(os.path.join(outdir, 'toms_web.' + ext), os.path.join(outdir, '%s.%s' % (ren, ext)))
print('versioned artifacts: %s.{js,wasm,data} | URL refs rewritten: %d'
      % (ren, new.count(ren)))
PY

  # ---- the page that actually gets published ------------------------------------------------
  # web/clear.html is hand-maintained SOURCE (a clean full-viewport page with no Emscripten
  # chrome). A rebuild regenerates the .js/.wasm/.data but never this file, so there is always
  # a clean page to deploy; stamping it here keeps the cache-busting version in sync.
  python3 - "$outdir/index.html" "web/clear.html" "$VER" <<'PY'
import sys
out, tpl, ver = sys.argv[1], sys.argv[2], sys.argv[3]
s = open(tpl, encoding='utf-8').read().replace('__TOMS_STAMP__', ver)
open(out, 'w', encoding='utf-8').write(s)
print('wrote %s from %s with v=%s' % (out, tpl, ver))
PY
}

TARGET="${1:-all}"
case "$TARGET" in
  all)
    build_one WebGL   web-gl   build-web
    build_one WebGPU  web-gpu  build-webgpu
    ;;
  webgl|WebGL|gl)
    build_one WebGL   web-gl   build-web
    ;;
  webgpu|WebGPU|gpu)
    build_one WebGPU  web-gpu  build-webgpu
    ;;
  *)
    echo "Usage: $0 [all|webgl|webgpu]" >&2
    exit 1
    ;;
esac

echo "[build_web] Done. Open web-launch.html (or web-gl/ and web-gpu/) in a browser."
