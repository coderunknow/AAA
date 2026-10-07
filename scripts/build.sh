#!/usr/bin/env bash
# Reproducible command-line build: host tools -> native headless (tests + smoke) -> web release.
# Requirements: CMake >= 3.24, Ninja, a C++20 compiler; for the web step an activated emsdk 4.0.11
# (EMSDK set) or EMSCRIPTEN_ROOT pointing at an Emscripten checkout. Extra args go to every configure.
set -euo pipefail
cd "$(dirname "$0")/.."

step() { printf '\n== %s\n' "$*"; }

step "host tools (bgfx shaderc)"
cmake --preset tools "$@"
cmake --build --preset tools --target shaderc

step "native headless: unit tests + smoke runs"
cmake --preset native-headless -DSDL_UNIX_CONSOLE_BUILD=ON "$@"
cmake --build --preset native-headless
ctest --preset native-headless

if [[ -z "${EMSCRIPTEN_ROOT:-}" && -n "${EMSDK:-}" ]]; then export EMSCRIPTEN_ROOT="$EMSDK/upstream/emscripten"; fi
if [[ -z "${EMSCRIPTEN_ROOT:-}" ]]; then
  echo "EMSCRIPTEN_ROOT/EMSDK not set: skipping the web build." >&2
  exit 0
fi
step "web release (WebAssembly)"
cmake --preset web-release "$@"
cmake --build --preset web-release
echo
echo "Done. Serve build/web-release/src/app/ with any static file server, e.g.:"
echo "  python3 -m http.server -d build/web-release/src/app 8080   # then open http://localhost:8080"
