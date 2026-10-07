# DESIGN_LOG

> Source of truth for design intent + append-only work log.
> Rules: read before every task; verify against the real project; append new entries, avoid editing old ones.

## Design Intent
(Exact ideas from the user, in their own words. Add bullets; do not silently reword.)

Source: the user's "MASTER GAME DEVELOPMENT PROMPT" (session 1, 2026-10-07). Key lines quoted verbatim:

- Goal: "a **high-quality playable 3D forest survival sandbox vertical slice for the desktop browser**, with a visual target inspired by modern AAA games and especially the cinematic realism and environmental presentation associated with games such as *Black Myth: Wukong*."
- "The game must remain an original project." / "Do not copy copyrighted assets, characters, environments, animations, branding, maps, UI, effects, compositions, or other protected content from reference games."
- Genre: "Third-person 3D forest survival simulator / sandbox."
- Core fantasy: "The player should feel like they are genuinely surviving in a dense, dangerous, believable wilderness." / "The world itself should communicate most of the information."
- "> **ONE EXTREMELY POLISHED VERTICAL SLICE**"
- Priority order: "1. Visual quality 2. Environmental immersion 3. Core interaction quality 4. World composition 5. Animation 6. Audio 7. VFX 8. UI/UX 9. Simulation depth 10. Feature quantity"
- Stack: "C++", "Emscripten", "WebAssembly", "bgfx", "bgfx's browser-compatible rendering path", "HTML/JavaScript only as a thin platform/bootstrap layer where necessary"
- "Do NOT use: Unity, Godot, Unreal Engine, Three.js, Babylon.js, PlayCanvas, other full game engines, other editor-driven game frameworks"
- "A library such as SDL3 may be used" for windowing/input/platform.
- Build: "CMake", "Ninja", "the native C++ compiler", "the Emscripten toolchain for browser builds"; "The canonical build process must be command-line reproducible."
- "Build a **small, purpose-built game framework** around the actual needs of this vertical slice."
- Visual: "**Photorealistic + Cinematic + Dramatic + AAA-inspired**"
- "Do not claim that browser WebGPU support exists merely because bgfx has a WebGPU backend."
- World: "Avoid a world that looks like: > flat terrain + random trees scattered everywhere."
- Survival: "Keep the simulation **semi-realistic**." / "Do not turn the game into a spreadsheet."
- NPCs: "Prioritize environmental life over complex social systems."
- Weather: "The objective is dramatic presentation, not feature count."
- Save: "Use persistent browser storage." via "a narrow platform abstraction"; versioned, validated, migration handling.
- Native dev build: "The native build must not become a separate game architecture."
- "Do not invent FPS values."
- Anti-hallucination: distinguish "implemented, compiled, linked, built, launched, tested, visually inspected, benchmarked, verified".
- Polish loop: "What is the single most obvious thing preventing this from looking like a polished commercial game?"
- Success: "> **A polished, visually impressive, playable, technically sound, browser-based third-person forest survival sandbox vertical slice built primarily in C++, rendered through bgfx, compiled to WebAssembly, launched independently through the browser, and independently verified through actual testing and visual inspection.**"
- "Do not optimize for the appearance of progress."

## Constraints & Decisions
- Process: maintain this DESIGN_LOG.md (user-supplied "design-log" skill): read + verify before every task, append-only.
- Open questions asked in session 1 were answered — see log entry #3.

## Log
<!-- Append new entries below. Do not edit old entries. -->

### #1 — 2026-10-07 — NOTE
Log created. Project state at creation: repo `coderunknow/AAA` contains only `LICENSE` (MIT, 2026 coderunknow); single commit `77790df Initial commit`. Working branch `arena/c97ff3bd-aaa`.

### #2 — 2026-10-07 — NOTE
Sandbox environment inspection (verified by running commands):
- Present: gcc/g++ 12.2.0 (Debian 12), GNU Make 4.3, Python 3.11.2, Node 22.22.3, npm 10.9.8, git 2.39.5, gh 2.23.0.
- Missing: cmake, ninja, clang, emcc/emsdk, Blender, any browser. No GPU (/dev/dri absent). 2 CPU cores, 3.8 GiB RAM, ~20 GB disk.
- Network: github.com git clone + codeload.github.com tarballs + api.github.com OK; PyPI and npm OK.
  BLOCKED: storage.googleapis.com (official emsdk binaries), objects.githubusercontent.com (GitHub release assets), raw.githubusercontent.com, media.githubusercontent.com (Git LFS), *.github.io.
- Available via allowed hosts (not yet installed/tested): cmake 4.4.4 + ninja 1.13.2 (PyPI); bgfx master @ cca91681c9 (2026-10-05); emscripten source (codeload); ziglang 0.16.0 wheel (bundles clang/lld, 93 MB); @yowasp/clang (wasm-hosted LLVM 22); npm `binaryen` 132 (JS build); @sparticuz/chromium 153 (headless Chromium, npm).
- npm packages `emsdk`, `emscripten-sdk`, `emsdk-npm` are thin wrappers that download from blocked hosts → not usable here.
- Implication: official emsdk cannot be installed in this sandbox; browser QA here would use software-rendered WebGL (no GPU) → FPS measured here is not representative of real hardware.

### #3 — 2026-10-07 — DECISION
User answers to session-1 questions (selected options):
- Toolchain: "Both" — unofficial Emscripten bootstrapped in sandbox (emscripten source + LLVM from Zig PyPI wheel + Binaryen from source) for local build/test; canonical documented build uses official emsdk + GitHub Actions workflow (official emsdk, optional GitHub Pages deploy).
- Target hardware for highest setting: "Integrated / laptop GPU" (e.g. Intel Iris Xe, Apple M1) — conservative budgets.
- Assets: "Procedural + AI textures + CC0" — procedural meshes/foliage/terrain/audio from C++/Python tools; AI-generated albedo textures with script-derived normal/roughness/AO; CC0 only if git-clonable; provenance manifest.
- Setting: "Misty East-Asian mountain forest" — pines, bamboo, mossy limestone outcrops, stream, abandoned (original) shrine landmark.
- Danger: "Environment + avoidable predators" — night cold, falls, hunger/thirst; a few wolves/boars that stalk and can be scared off by fire/torch; no combat system.
- First-session milestone: "Foundation + first rough playable" — toolchain, CMake, native+WASM builds, bgfx in headless Chromium, shader pipeline, input, frame loop, tests, plus sculpted terrain, first foliage, stand-in player, follow camera, lighting/fog pass, verified in browser.

### #4 — 2026-10-07 — DECISION
Engineering decisions made while building the foundation (each verified by building/running as noted):
- Working title **"Mistpine"** (original name; CMake project + window title).
- Browser backend: bgfx **OpenGL ES 3.0 → WebGL 2**. Verified at runtime in headless Chromium: bgfx reports "OpenGL ES 3.0", WebGL renderer string "ANGLE (Google, Vulkan 1.3.0 (SwiftShader Device ...))". WebGPU NOT used/claimed (`navigator.gpu` absent in the sandbox browser; bgfx WebGPU backend off).
- bgfx.cmake pinned at f2ea8fb (bgfx 1.164.9149). This revision's API differs from older docs: window described by `Init::swapChain`, `reset(flags, SwapChain*)`, no INSTANCING/COMPARE caps bits (baseline).
- Emscripten 4.0.11 only defines lower-case `__EMSCRIPTEN_major__`; bx expects upper-case → force-included `cmake/emscripten_version_compat.h` in web builds. Web builds use `-msimd128` (bx adds `-msse4.2`). `bimg_encode` excluded from ALL on web (x86 intrinsics; not needed at runtime).
- `-gsource-map` dropped from web-dev (sandbox LLVM has no llvm-dwarfdump).
- Headless native build: bgfx Noop renderer + `SDL_UNIX_CONSOLE_BUILD` (no X11/GL headers in sandbox). Windowed native build is NOT verified here.
- bgfx program link requires VS outputs == FS inputs exactly; all shadow VS output `v_texcoord0` so one `fs_shadow` serves all casters.
- Rendering: views in Sequential mode; frame uniforms set before the first submit; shadow map rebound per scene draw (bgfx bindings last one submit). Post pass must write alpha (otherwise the WebGL canvas composites as transparent — observed as a blank page-coloured frame, fixed).
- Save format v2 (JSON, `mistpine-save`), v1→v2 migration path, validation; storage = localStorage (web) / SDL pref-path file with write-then-rename (native).
- Grass is a separate near-player "detail" stream (radius 70 m, release 110 m) so streaming never holds grass for the full 300 m radius.

### #5 — 2026-10-07 — DONE
Verified results (this sandbox: 2 CPU cores, no GPU):
- Unit tests: **27/27 pass** (`build/unit/tests/aaa_tests`), incl. 3 new save tests.
- shaderc built from bgfx sources (≈30 min CPU); all 15 shaders compile to GLSL ES 3.00.
- `native-headless` build: `mistpine --headless --frames 300` runs world gen → renderer load → 300 frames → clean exit, no errors (Noop renderer: proves logic/API usage, not pixels).
- `web-release` build: index.wasm 1.67 MB, index.data 46 KB. In headless Chromium (SwiftShader software WebGL2), `?quality=low` 960×540: loading complete in 2.5 s, frames presented, no console errors. First visually inspected frame: `docs/qa/2026-10-07-first-frame-low-swiftshader.png`.
- Performance: ~1 FPS under SwiftShader on 2 CPU cores. NOT representative of any GPU; no real-hardware FPS has been measured.
- Visual QA of first frame (honest): pipeline works (terrain, instanced pines, cascaded shadows, sky/clouds, character, grass, tonemap) but look is far from target — no mist, over-exposed pale terrain, lime foliage, grass card band at near plane, camera too low. Next: visual polish loop.

## #6 — 2026-10-07 — DONE: visual retune pass, stream water, look-input settle fix
- Retune (verified visually in qa/run4–run7, web-release, quality=low, 960×540, SwiftShader software WebGL2 — not representative of GPU performance; ~0.95 rAF/s measured here): sky/fog/exposure retuned, layered valley mist, darker rock/dirt/moss/grass terrain layers, rust-brown needle litter, dithered near-camera foliage fade.
- Grass clump rebuilt (7/4/2 splayed cards, varied heights, root AO); grass excluded from the trail (smoothstep(0.9,2.4,trail)); spacing 0.62 m.
- Water: new `WaterRenderer` (ribbon along stream polyline, 48 m culled segments) + `vs/fs_water.sc` (Fresnel sky reflection, scrolling ripple normals, sun glint, tannin body, edge foam, fog). Built (native-headless 120 frames clean, web-release), visually inspected in qa/run5 and qa/run7: reads as a stream with sky reflection; 0 console errors.
- CONTRADICTION found & fixed: identical "still" playtests produced different camera framing (run5 vs run6). Cause (inferred, consistent with known Chrome behaviour): a spurious large mouse movement around the pointer-lock request on click. Fix: drop single motion events >300 px and ignore look deltas for 3 frames after any pointer-lock state change. Verified: run7 full scripted playtest gives the stable over-shoulder framing.
- Unit tests: 27/27 pass.
- Known visual gaps (not yet addressed): grass cards still too saturated/yellow-lime at close range; character is a blocky stand-in; pine crowns read as flat cards from below; distant mountains pale/flat.

### #7 — 2026-10-07 — INTENT
User: "Now build the finish version." / "Create PR and merge after you have finished."
Scope for the finished vertical slice (derived from the master prompt + #3 answers; no new features beyond them):
- Survival loop: warmth (night cold, wet from wading), hunger, thirst, health; fall damage; collapse → wake at last rest point (no hard game over).
- Interaction: forage berries/mushrooms, gather dry branches + flint, drink from the stream, build a campfire, rest at the shrine (save point).
- Shrine landmark (original design) on the terrace; campfire with flame VFX and a local fire light.
- Avoidable predators: wolves that roam/stalk at night, are scared off by fire and avoid the shrine; a bite hurts, no combat.
- Procedural audio (C++ synth via SDL3 audio): wind, stream, birds by day, insects by night, fire crackle, footsteps, distant howls.
- HUD (DOM, thin): vitals, inventory, prompts, notifications; debug overlay only with ?debug=1.
- Save v3 hooked in (autosave, rest, pause) with v1/v2 → v3 migration.
- scripts/build.sh, README, GitHub Actions (official emsdk 4.0.11) + Pages; final report.
NOTE: sandbox was recycled since #6 — local branch had been reset to 77790df with files untracked; verified working tree == origin dd5e6df, re-pointed branch (git reset to origin, no file changes). Toolchain/deps/build dirs had to be rebuilt.

## #8 — 2026-10-07 — DONE: finished vertical slice (scope of #7)
Verified in this sandbox (2 CPU cores, no GPU; browser = headless Chromium 153, SwiftShader software WebGL2, bgfx OpenGL ES 3.0 backend):
- Implemented: shrine landmark (`src/world/shrine.*`, `procgen/structure_meshes.*`); survival (warmth/wetness/food/water/health, fall damage, collapse → wake at last rest); interactables (branches, flint, berries, mushrooms with regrowth; drink; campfire build/feed/burn-out); wolves (roam/stalk/flee fire, avoid shrine, bite; rigid-part pose); fire VFX + flickering local fire light; procedural audio (`src/audio/soundscape.*`) on an SDL3 audio stream (`platform/audio_output.*`); app wiring (title/continue screen, pause, autosave 45 s/pause/focus-loss/rest, event → sfx + notifications, HUD feed); DOM HUD; debug overlay gated behind ?debug=1; QA scenarios ?qa=camp|shrine|wolves; save v3 hooked in.
- Unit tests: **42/42 pass** (`build/unit`), incl. test_survival (gather → fire → warm → save end-to-end) and test_audio.
- `native-headless`: built with no warnings from project sources; `ctest`: **4/4 pass** (unit + smoke_day, smoke_night_camp, smoke_wolves; Noop renderer = logic only).
- `web-release`: built (wasm 1.8 MB, js 0.22 MB, data 64 KB). Playtests run8–run12: ready in 3.4–5.0 s, **0 console errors**; visually inspected title, day, night campfire, shrine, night wolf (curated copies in `docs/qa/2026-10-07-final-*.png`).
- Save round trip verified in the browser (run12, `--script persist`): pause → localStorage 394 B v3 JSON → reload → title shows "Continue" → "restored from save".
- Polish fixes found by visual QA: (1) title showed blank fog — camera was not updated while paused → camera/animator now update during pause; (2) moon shadow cut a dark-red hole into firelight → shadows lifted by firelight × (1−daylight); (3) moonlit grass read neon green → scotopic desaturation in tonemap below ~0.1 luminance; (4) shrine terrace bare → overgrown grass/ferns; (5) HUD legibility (soft backdrops, brighter clock).
- NOT verified: audio has not been listened to by a human (unit-tested + device opens); native windowed build (CI only); any real-GPU performance (~0.76–1 rAF/s under SwiftShader, not representative); GitHub Pages deployment (needs Pages enabled).
- Added: README.md, docs/ASSETS.md (all assets generated in code; no AI/CC0 assets were used), scripts/build.sh, .github/workflows/ci.yml (official emsdk 4.0.11).
