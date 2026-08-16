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

  # Copy the linked artifacts out (both share the filename tower_vulkan_web.*)
  mkdir -p "$outdir"
  cp -f web/tower_vulkan_web.html \
        web/tower_vulkan_web.js  \
        web/tower_vulkan_web.wasm \
        web/tower_vulkan_web.data "$outdir/"
  echo "[build_web] $backend artifacts -> $outdir/:"
  ls -la "$outdir"

  # ---- cache-busting + real-error display: stamp the generated HTML ----
  # A normal reload then always fetches fresh .wasm/.data (no hard-refresh),
  # and the page shows the REAL error text instead of the generic
  # "Exception thrown, see JavaScript console" string.
  local VER="$(git rev-parse --short HEAD 2>/dev/null || echo dev)-$(date +%Y%m%d%H%M%S)"
  for html in "$outdir/tower_vulkan_web.html" "web/tower_vulkan_web.html"; do
    [ -f "$html" ] || continue
    python3 - "$html" "$VER" <<'PY'
import sys
html, ver = sys.argv[1], sys.argv[2]
s = open(html, encoding='utf-8').read()
# 1) define window.TOMS_VERSION early (in <head> so it exists before Module)
tag = '<script>window.TOMS_VERSION=%r;</script>' % ver
if 'window.TOMS_VERSION' not in s:
    s = s.replace('<head>', '<head>\n    '+tag, 1)
# 2) inject locateFile into the Module object (right after "var Module = {")
if 'locateFile' not in s:
    s = s.replace('var Module = {',
        'var Module = {\n        locateFile: function(p, prefix) {\n'
        '          if ((p.endsWith(\'.wasm\') || p.endsWith(\'.data\')) && window.TOMS_VERSION) '
        'return (prefix||\'\') + p + \'?v=\' + window.TOMS_VERSION;\n'
        '          return (prefix||\'\') + p;\n        },', 1)
# 3) replace the generic window.onerror with one that shows the REAL message+stack
OLD = """      window.onerror = window.onunhandledrejection = () => {
        // TODO: do not warn on ok events like simulating an infinite loop or exitStatus
        setStatus('Exception thrown, see JavaScript console');
        spinnerElement.style.display = 'none';
        setStatus = (text) => {
          if (text) console.error('[post-exception status] ' + text);
        };
      };"""
if OLD in s and 'showErr' not in s:
    NEW = """      function showErr(msg){
        var bar = document.getElementById('tomserr');
        if(!bar){ bar = document.createElement('div'); bar.id='tomserr';
          bar.style.cssText='position:fixed;left:0;right:0;top:0;z-index:9999;background:#400;color:#fff;font:14px/1.4 monospace;white-space:pre-wrap;padding:10px;max-height:60%;overflow:auto;';
          (document.body||document.documentElement).appendChild(bar); }
        bar.textContent = 'TOMS error:\\n' + msg;
        console.error('[TOMS] ' + msg);
      }
      window.onerror = window.onunhandledrejection = (e) => {
        var msg = (e && e.message) ? e.message : String(e);
        if (e && e.error && e.error.stack) msg += '\\n' + e.error.stack;
        else if (e && e.reason) msg += '\\n' + (e.reason && e.reason.stack ? e.reason.stack : e.reason);
        showErr(msg);
        spinnerElement.style.display = 'none';
        setStatus = (text) => { if (text) console.error('[post-exception status] ' + text); };
      };
      if (typeof Module !== 'undefined') Module.onAbort = function(what){ showErr('ABORT: ' + (what||'unknown')); };"""
    s = s.replace(OLD, NEW, 1)
open(html, 'w', encoding='utf-8').write(s)
print('stamped', html, 'with v='+ver, '| showErr:', 'showErr' in s)
PY
  done
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
