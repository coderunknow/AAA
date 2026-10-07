#!/usr/bin/env python3
"""Sandbox stand-in for llvm-nm (Zig does not ship one).

Emscripten only uses llvm-nm as $NM for autoconf-style builds and via `emnm`;
it is not used when linking. This stub exists so tool-existence checks pass and
fails loudly if anything actually tries to rely on it.
"""
import sys
if "--version" in sys.argv:
    print("LLVM (sandbox llvm-nm stub) version 21.1.0")
    sys.exit(0)
sys.stderr.write("llvm-nm stub: symbol listing is not supported in the sandbox toolchain\n")
sys.exit(1)
