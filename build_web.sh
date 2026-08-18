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
    NEW = """      function showErr(msg){
        var bar = document.getElementById('tomserr');
        if(!bar){ bar = document.createElement('div'); bar.id='tomserr';
          bar.style.cssText='position:fixed;left:0;right:0;top:0;z-index:9999;background:#400;color:#fff;font:14px/1.4 monospace;white-space:pre-wrap;padding:10px;max-height:60%;overflow:auto;';
          (document.body||document.documentElement).appendChild(bar); }
        bar.textContent = 'TOMS error: ' + msg;
        console.error('[TOMS] ' + msg);
      }
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
