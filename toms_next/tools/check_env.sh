#!/usr/bin/env bash
# check_env.sh -- WSL/Linux twin of tools/check_env.ps1.
#
# Why it exists: the Windows checker reports missing prerequisites with popups and download links.
# There are no popups in a terminal, so this prints the same facts as a pass/fail list with the exact
# fix for each failure -- the web build needs a different set of tools than the desktop one (emsdk and
# node instead of MSVC), so both are checked here.
set -u
ok=0; bad=0
say()  { printf '  %-28s %s\n' "$1" "$2"; }
need() { # need <name> <resolved-path-or-empty> <fix>
  if [ -n "$2" ]; then say "$1" "OK  $2"; ok=$((ok+1)); else say "$1" "MISSING -- $3"; bad=$((bad+1)); fi; }

echo "== toms_next prerequisites (Linux/WSL) =="
need "cmake (>=3.24)"   "$(command -v cmake || true)"      "install cmake 3.24+ (or use \$HOME/opt/cmake/bin/cmake)"
need "ninja"            "$(command -v ninja || true)"      "apt install ninja-build (or pip install ninja)"
need "git"              "$(command -v git || true)"        "needed by FetchContent for bgfx/SDL3/imgui/glm"
need "node"             "$(command -v node || true)"       "needed as the cross-compiling emulator for web tests"
need "python3"          "$(command -v python3 || true)"    "the local web server (python3 -m http.server)"

# emsdk: the toolchain file is what CMake actually needs, so check for that path, not just a binary.
EMSDK_DIR="${EMSDK:-$HOME/opt/emsdk}"
if [ -f "$EMSDK_DIR/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake" ]; then
  say "emscripten toolchain" "OK  $EMSDK_DIR"
  v=$("$EMSDK_DIR/upstream/emscripten/emcc" --version 2>/dev/null | head -1 | sed 's/.*) //')
  say "emcc version" "${v:-unknown}"
  ok=$((ok+1))
else
  say "emscripten toolchain" "MISSING -- set EMSDK, or install: git clone https://github.com/emscripten-core/emsdk $HOME/opt/emsdk && $HOME/opt/emsdk/emsdk install latest && $HOME/opt/emsdk/emsdk activate latest"
  bad=$((bad+1))
fi

echo
echo "  $ok ok, $bad missing"
[ "$bad" -eq 0 ] || exit 1
echo "  Web build: cmake --preset web-release && cmake --build --preset web-release"
