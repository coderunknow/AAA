#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Sandbox-only toolchain bootstrap.
#
# The CANONICAL build uses the official emsdk (see README.md / tools/setup_emsdk.sh
# and .github/workflows/build.yml). This script exists for development
# environments where the official emsdk download hosts (storage.googleapis.com)
# and GitHub release assets are unreachable, but github.com/codeload, PyPI and npm
# are reachable. It assembles an equivalent Emscripten 4.0.11 toolchain from:
#
#   * Emscripten 4.0.11 source            (codeload.github.com)
#   * LLVM/Clang/LLD 21.1 from the Zig 0.16 PyPI wheel (`zig clang`, `zig wasm-ld`)
#   * Binaryen version_123, built from source (codeload.github.com)
#   * CMake + Ninja from PyPI
#   * Node.js from the host
#
# Emscripten 4.0.11 expects LLVM 21 and Binaryen 123, so the versions match.
#
# Usage:  tools/sandbox/bootstrap_toolchain.sh [phase...]
#         phases: python emscripten binaryen llvm config browser all (default: all)
# Result: $AAA_TOOLCHAIN (default ~/.cache/aaa-toolchain) and an env file
#         $AAA_TOOLCHAIN/env.sh to `source`.
# -----------------------------------------------------------------------------
set -euo pipefail

EMSCRIPTEN_VERSION=4.0.11
BINARYEN_VERSION=123
T="${AAA_TOOLCHAIN:-$HOME/.cache/aaa-toolchain}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
JOBS="${JOBS:-$(nproc)}"
mkdir -p "$T"

phase_python() {
  [ -x "$T/venv/bin/python" ] || python3 -m venv "$T/venv"
  "$T/venv/bin/pip" install -q --upgrade "cmake>=3.24" ninja "ziglang==0.16.0"
}

zig_dir() { "$T/venv/bin/python" -c "import ziglang,os;print(os.path.dirname(ziglang.__file__))"; }

phase_emscripten() {
  if [ ! -f "$T/emscripten/emcc.py" ]; then
    rm -rf "$T/emscripten" "$T/emscripten.tgz"
    curl -fsSL -o "$T/emscripten.tgz" \
      "https://codeload.github.com/emscripten-core/emscripten/tar.gz/refs/tags/$EMSCRIPTEN_VERSION"
    mkdir -p "$T/emscripten"
    tar -xzf "$T/emscripten.tgz" -C "$T/emscripten" --strip-components=1
    rm -f "$T/emscripten.tgz"
  fi
  # Emscripten's JS tooling (acorn, terser, ...) lives in node_modules.
  if [ ! -d "$T/emscripten/node_modules" ]; then
    (cd "$T/emscripten" && npm ci --omit=dev --no-audit --no-fund --loglevel=error)
  fi
}

phase_binaryen() {
  if [ ! -x "$T/binaryen/bin/wasm-opt" ]; then
    local src="$T/binaryen-src"
    if [ ! -f "$src/CMakeLists.txt" ]; then
      rm -rf "$src"; mkdir -p "$src"
      curl -fsSL "https://codeload.github.com/WebAssembly/binaryen/tar.gz/refs/tags/version_$BINARYEN_VERSION" \
        | tar -xz -C "$src" --strip-components=1
    fi
    "$T/venv/bin/cmake" -S "$src" -B "$src/build" -G Ninja \
      -DCMAKE_MAKE_PROGRAM="$T/venv/bin/ninja" \
      -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF -DBUILD_STATIC_LIB=ON \
      -DENABLE_WERROR=OFF -DCMAKE_INSTALL_PREFIX="$T/binaryen"
    "$T/venv/bin/cmake" --build "$src/build" -j "$JOBS" --target \
      wasm-opt wasm-metadce wasm-emscripten-finalize wasm-ctor-eval wasm-split wasm-dis wasm-as wasm2js
    mkdir -p "$T/binaryen/bin"
    cp "$src"/build/bin/* "$T/binaryen/bin/"
    # Emscripten reads binaryen's JS helpers from the src tree only for wasm2js; not needed.
  fi
}

phase_llvm() {
  # Thin wrappers so Emscripten sees a conventional LLVM bin directory.
  local Z; Z="$(zig_dir)"
  local B="$T/llvm/bin"
  mkdir -p "$B" "$T/llvm/lib/clang/21"
  # clang resource dir (builtin headers like stddef.h, wasm_simd128.h) from Zig.
  ln -sfn "$Z/lib/include" "$T/llvm/lib/clang/21/include"
  w() { printf '#!/bin/sh\nexec "%s/zig" %s "$@"\n' "$Z" "$2" > "$B/$1"; chmod +x "$B/$1"; }
  # -resource-dir points clang at the headers above; zig's own CWD-relative lookup
  # does not match the conventional layout.
  printf '#!/bin/sh\nexec "%s/zig" clang -resource-dir "%s" "$@"\n' "$Z" "$T/llvm/lib/clang/21" > "$B/clang"
  printf '#!/bin/sh\nexec "%s/zig" clang --driver-mode=g++ -resource-dir "%s" "$@"\n' "$Z" "$T/llvm/lib/clang/21" > "$B/clang++"
  chmod +x "$B/clang" "$B/clang++"
  w wasm-ld wasm-ld
  w llvm-ar ar
  w llvm-ranlib ranlib
  # llvm-nm is not shipped by Zig; a small Python stand-in
  # covers the subset Emscripten uses (defined/undefined symbols of wasm objects).
  cp "$REPO_ROOT/tools/sandbox/llvm-nm.py" "$B/llvm-nm"; chmod +x "$B/llvm-nm"
  # Zig's objcopy is ELF-only; Emscripten needs wasm custom-section stripping.
  cp "$REPO_ROOT/tools/sandbox/llvm-objcopy.py" "$B/llvm-objcopy"; chmod +x "$B/llvm-objcopy"
}

phase_config() {
  cat > "$T/emscripten.config" <<EOF
LLVM_ROOT = '$T/llvm/bin'
BINARYEN_ROOT = '$T/binaryen'
NODE_JS = '$(command -v node)'
CACHE = '$T/emcache'
EOF
  cat > "$T/env.sh" <<EOF
# source this file
export AAA_TOOLCHAIN='$T'
export EM_CONFIG='$T/emscripten.config'
export EMSCRIPTEN_ROOT='$T/emscripten'
export EMSDK_PYTHON='$(command -v python3)'
export PATH='$T/emscripten':'$T/venv/bin':"\$PATH"
EOF
  echo "Toolchain ready. Run: source $T/env.sh"
}

phase_browser() {
  # Headless Chromium (software WebGL2 via SwiftShader) for automated browser QA.
  (cd "$REPO_ROOT/tools/browser" && npm install --no-audit --no-fund --loglevel=error)
}

phases=("$@"); [ ${#phases[@]} -eq 0 ] && phases=(all)
for p in "${phases[@]}"; do
  case "$p" in
    all) phase_python; phase_emscripten; phase_llvm; phase_binaryen; phase_config; phase_browser ;;
    *) "phase_$p" ;;
  esac
done
