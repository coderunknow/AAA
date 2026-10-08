# Changelog

All notable changes to Mistpine are documented here. The format is loosely based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versions follow the CMake project version
(`CMakeLists.txt` is the single source of truth).

## [0.1.0] — 2026-10-08 — stability, graphics, animation and instant-run release

The first release. The finished browser vertical slice (PR #1) becomes a verified, shippable
release: more stable, clearly better looking, with real procedural character animation, portable
desktop builds and a web build, every artifact runnable immediately after download.

### Stability and cross-platform (M1)

- **Fixed the native windowed CI build.** Root cause: SDL3 fails configure on Linux when the X11
  dev packages are incomplete (`SDL_X11_XTEST`/`XFIXES`/`XRENDER` are fatal when their headers are
  missing); the job now installs `libxtst-dev`, `libxfixes-dev` and `libxrender-dev`.
- **Deterministic single-threaded rendering**: `bgfx::renderFrame()` is called before
  `bgfx::init()`, so frame submission order no longer depends on an internal render thread.
- **Backend selection** with the platform preference chain (Windows D3D11 → Vulkan → OpenGL,
  macOS Metal, Linux OpenGL → Vulkan, web WebGL2) and a `--renderer d3d11|vulkan|opengl|metal|noop`
  override; the selected backend is logged.
- **Shader profiles per backend**: `essl` (web), `glsl` (Linux OpenGL), `spirv` (Linux Vulkan),
  `dx11` (`--platform windows -p s_5_0`, compiled by a Windows shaderc in CI because the pinned
  Linux shaderc cannot emit SM 5.0), `metal` (`--platform osx -p metal`). Every
  `bgfx::RendererType` maps explicitly; a missing profile fails clearly instead of silently
  falling back to `essl`.
- **Portable assets**: no compile-time absolute asset path. Assets are discovered relative to
  the executable (zip/tar/AppImage layout, build tree, macOS `Contents/Resources/assets`),
  independent of the current working directory; `--assets <path>` overrides. Verified by launching
  the packaged layout from a foreign directory.
- **Fixed-step simulation at 60 Hz** with render interpolation (camera and skinned-character
  palettes) and a hitch clamp; animation and camera smoothing are frame-rate independent.
  Pause-on-focus-loss preserved.
- **Save format v4** (adds master volume + fullscreen; v3 saves migrate with defaults).
  Corrupt-save fuzz test: 4000 deterministic mutations never crash and always return a defined
  success/failure result without touching the output on failure.
- **Robustness**: localStorage quota / `SecurityError` handling tells the player once and keeps
  running; unwritable native preference paths degrade the same way; audio device loss and missing
  audio devices are handled; WebGL context loss shows the error overlay.
- `--version` prints `Mistpine 0.1.0` (CMake `project(... VERSION 0.1.0)` is the source of truth);
  the version is logged at startup and shown on the title screen.
- CI hardening: `-Werror` (GCC/Clang/em++) and `/WX` (MSVC) for first-party targets; an
  ASan + UBSan job (unit tests + smoke runs); shader-profile jobs compiling every profile
  (host-all on Linux, dx11 on a Windows runner).

### In-engine UI (M1)

- **Menus, HUD, prompts, toasts and a Settings screen are now drawn in-engine** (C++/bgfx) and
  shared by web and native builds. The DOM shell keeps only the loading overlay and
  fatal-error / WebGL2-unsupported messaging.
- SDF font atlas generated at load time from **Source Serif 4** (SIL OFL-1.1, bundled with the
  license text in `assets/fonts/`), rasterised with stb_truetype (MIT / public domain, bundled
  with the pinned bgfx). Serif title, quiet vitals that fade while healthy, italic toasts.
- Title screen with *Continue* / *Start a new journey* (two-click confirmation), pause screen,
  and a Settings section (quality, master volume, mouse sensitivity, invert Y, fullscreen on
  native) persisted in the save.
- The browser harness reads a read-only `window.__mistpineState` JSON snapshot
  (menu/phase/vitals/noLock rects).

### Release pipeline (M2)

- **`.github/workflows/release.yml`**: builds from an explicit commit SHA and supports a dry run
  (`tag=v0.1.0-rc` publishes nothing). Produces `mistpine-v0.1.0-web.zip`,
  `mistpine-v0.1.0-web-singlefile.html`, `mistpine-v0.1.0-windows-x64.zip`,
  `mistpine-v0.1.0-linux-x86_64.AppImage`, `mistpine-v0.1.0-linux-x86_64.tar.gz`,
  `mistpine-v0.1.0-macos-arm64.zip` and `SHA256SUMS.txt`.
- **Single-file web build** (`-sSINGLE_FILE=1` + embedded assets, no fetch/XHR) that launches from
  `file://` in Chrome; persistence may be unavailable there, in which case the player is told
  once and the game stays playable.
- Windows x64: MSVC, static CRT, `WIN32_EXECUTABLE`, D3D11 primary backend, `dumpbin` dependency
  audit. Linux x86_64: AppImage + tar.gz on ubuntu-22.04, `ldd` audit, xvfb + Mesa llvmpipe
  OpenGL render verification with `--screenshot`. macOS arm64: `Mistpine.app` (Metal, Info.plist,
  ad-hoc signed, not notarized), `otool` audit.
- **Packaged-binary smoke test** on every platform: run from a foreign working directory,
  assets found, no `ERROR`-level log lines.
- **Screenshot capture** (`--screenshot <path>` / `--screenshot-frame <n>`, bgfx callback → PNG).
- Browser CI job runs the playtest harness (default + persist scripts) with zero console errors
  enforced; `launch.mjs` honours `CHROME_PATH` and playtest supports `--file` (no HTTP server).

### Procedural skinned player and wolf (M3)

- Both the player and the wolf are now **procedural skinned meshes** with real procedural
  animation; the rigid-part pipeline is retired from the render path.
- Skeletons generated in code: 22 player joints (pelvis, spine chain, neck, head, clavicles,
  arms, hands, thighs, calves, feet, toes) and 26 wolf joints (spine, neck, head, jaw, four
  three-segment legs, tail chain).
- Smooth skinned meshes generated in code (normals, UVs, normalized weights, ≤ 4 influences per
  vertex), cached once per quality level during loading. Layered procedural clothing for the
  player (jacket, trousers, wrap, straw hat, backpack); procedural fur cues for the wolf.
- GPU skinning with a 32-matrix joint palette (within WebGL2 limits), dedicated skinned shaders
  with shadow support.
- Procedural locomotion: speed-driven gait with stride/cadence, pelvis bob and sway,
  counter-rotating arm swing, acceleration and turning lean; **two-bone foot IK** that plants
  feet during stance (measured drift < 2 cm), adapts to terrain and adjusts pelvis height on
  slopes.
- Player states (blended): idle breathing and weight shifts, crouch, jump, landing squash,
  wading, gather, kneeling at the fire, drinking, shivering when cold; clamped, damped look-at.
- Wolf animation: walk/trot/lope, spine flex, ear spring, tail spring chain, crouch-stalk, flee.
- Animation tests: weights sum to 1, ≤ 4 influences, bind pose reproduces the rest mesh, IK
  converges and respects joint limits, planted-foot drift < 2 cm, no NaNs over long randomized
  input runs.

### Balanced cinematic graphics pass (M4)

- **Bloom**: half-resolution bright-pass + separable gaussian downsample/filter/upsample chain,
  added in linear light before the filmic response (Low: off; Medium: single iteration;
  High: two iterations).
- **Sun shafts**: half-resolution screen-space radial blur through mist, gated to High.
- **SSAO**: half-resolution depth-based hemisphere occlusion with a separable blur, applied in
  the tonemap pass (High only; capability fallback when no sampleable depth format exists).
  Baked terrain/canopy vertex AO remains on all quality levels.
- **FXAA-lite**: luma-based edge detection with a direction-aware blend in the tonemap pass
  (Medium/High). TAA deliberately not shipped.
- **Forest**: pine crowns gained crossed horizontal pads so they read fuller from below; widened
  per-instance grass hue range (grey-green to seed-head yellow) alongside the existing height
  and clumping variation. Hierarchical wind, trunk sway, branch motion, leaf flutter and
  valley-scale gusts were already in place and are unchanged.
- **Terrain**: moss/rock/soil/litter material layers with wet banks, triplanar rock, procedural
  detail normals — unchanged this pass (already at target).
- **Time-of-day grading** (dawn/morning/midday/dusk/night) and the filmic ACES response with
  scotopic night shift were already in place and are unchanged.
- **Benchmark**: `--bench [path]` / `?bench=1` runs a deterministic camera route through day,
  dusk and night-with-campfire and writes JSON frame-time statistics (CPU timing, and bgfx GPU
  timing where the backend exposes it).
- Deferred (recorded in `DESIGN_LOG.md`): distant tree impostors, distant mountain silhouettes,
  water refraction, shore foam, campfire smoke, heat shimmer, TAA.

### QA and polish (M5)

- Fixed visual QA matrix: `?qa=spawn|character|dusk|dawn|camp|shrine|wolves` plus the title
  screen, captured as curated screenshots under `docs/qa/v0.1.0-*.png`.
- In-engine UI: a click anywhere on the title/pause veil starts or resumes the journey (only
  the quiet "Start a new journey" button is excepted), matching the original DOM behaviour.

### Known limitations

- Characters and wolves are skinned meshes, but a residual leg-motion discontinuity remains on a
  few frames where a gesture/crouch state changes at a walk (measured up to 0.11 m above the
  speed-scaled limb-motion bound; not a NaN, sliding foot or IK failure). See `DESIGN_LOG.md` #14.
- macOS builds are ad-hoc signed, **not notarized**; unsigned Windows builds may trigger
  SmartScreen ("More info → Run anyway").
- The web build uses bgfx's WebGL2 (OpenGL ES 3.0) backend. WebGPU is not used or claimed.
- No real-GPU benchmark has been run in the build sandbox (software rendering only); numbers
  from the sandbox are SwiftShader/software numbers and say nothing about GPU performance.
- GitHub Pages deployment requires Pages to be enabled on the repository (a repository setting
  the agent's token cannot change); until then the Pages deploy job reports the blocker.
