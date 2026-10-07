/* Force-included in web builds.
 * bx (bgfx.cmake f2ea8fb) detects Emscripten via __EMSCRIPTEN_MAJOR__/MINOR__/TINY__,
 * but Emscripten 4.0.11 (pinned: last release paired with LLVM 21) only defines the
 * lower-case __EMSCRIPTEN_major__ family in <emscripten/version.h>. Without this, bx
 * evaluates BX_PLATFORM_EMSCRIPTEN to 0 and fails to compile. */
#pragma once
#include <emscripten/version.h>
#ifndef __EMSCRIPTEN_MAJOR__
#define __EMSCRIPTEN_MAJOR__ __EMSCRIPTEN_major__
#define __EMSCRIPTEN_MINOR__ __EMSCRIPTEN_minor__
#define __EMSCRIPTEN_TINY__ __EMSCRIPTEN_tiny__
#endif
