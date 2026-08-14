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
