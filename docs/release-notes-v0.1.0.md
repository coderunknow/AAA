# Mistpine v0.1.0

*A valley in the clouds — a forest survival journey.*

This is the first release. The finished browser vertical slice becomes a verified, shippable
build: more stable, clearly better looking, with real procedural character animation, portable
desktop artifacts and a web build — every one of them runnable immediately after download, with
no installer and no build step.

## ⚠️ These are unsigned desktop builds

- **Windows:** SmartScreen may warn that the app is unrecognised. Choose
  **More info → Run anyway**.
- **macOS:** the app is **ad-hoc signed, not notarized**. Use **right-click → Open**, or
  **System Settings → Privacy & Security → Open Anyway**.

No commercial signing certificate is involved, and the macOS build has **not** been notarized.

## Downloads

| Asset | Platform | How to run |
|---|---|---|
| `mistpine-v0.1.0-web.zip` | any static web host | Upload the four files and open `index.html`. |
| `mistpine-v0.1.0-web-singlefile.html` | Chrome | Double-click it, or open it over `file://`. One self-contained HTML — no server needed. |
| `mistpine-v0.1.0-windows-x64.zip` | Windows x64 (Windows 10 or later) | Unzip, then run `Mistpine.exe`. |
| `mistpine-v0.1.0-linux-x86_64.AppImage` | Linux x86_64 | `chmod +x …AppImage`, then run it. |
| `mistpine-v0.1.0-linux-x86_64.tar.gz` | Linux x86_64 | `tar xzf …tar.gz`, then run `mistpine`. |
| `mistpine-v0.1.0-macos-arm64.zip` | macOS (Apple silicon) | Unzip, then open `Mistpine.app`. |

`SHA256SUMS.txt` covers every other asset above.

## What's new

### Stability and cross-platform

- Deterministic single-threaded rendering — frame submission no longer depends on a bgfx
  internal render thread.
- Explicit backend selection with a `--renderer d3d11|vulkan|opengl|metal|noop` override.
- Every shader profile (`essl`, `glsl`, `spirv`, `dx11`, `metal`) is built for the backend that
  needs it; a missing profile is a hard error, never a silent fallback.
- Portable asset lookup: the executable finds its assets from any working directory.
- Fixed-step 60 Hz simulation with render interpolation, frame-rate-independent animation and
  camera smoothing, and a hitch clamp.
- Save format v4; **existing v3 saves load and migrate automatically**. 4000 deterministic
  corrupt-save mutations neither crash nor corrupt the output.

### Graphics

- Time-of-day grading with dawn / morning / midday / dusk / night presets.
- Contact shadows grounding characters and props.
- Volumetric-ish light shafts, bloom, screen-space ambient occlusion, improved forest crowns and
  terrain shading, cinematic colour grading.

### Animation

- Player and wolves are **procedural skinned meshes** with a real skeleton and GPU skinning.
- Procedural locomotion with a proper gait, **foot IK**, secondary motion and blended state
  transitions (idle / walk / run / crouch / gesture), plus head look-at.

### Release pipeline

- Portable Windows `.zip`, Linux AppImage + `.tar.gz`, macOS `.app` in a `.zip`, a Pages web
  build and a single-file HTML, all checksummed.

## Controls

`WASD` move · `Shift` run · `Ctrl` crouch · `Space` jump · `E` interact / forage · `F` craft ·
`Tab` inventory · `Esc` pause · mouse look.

## Platform notes

- **Windows:** the artifact is a genuine 64-bit image and carries **no toolchain runtime DLLs** —
  no SDL, no bgfx, no MinGW runtime, no Visual C++ redistributable. It does import the Windows
  Universal CRT (`api-ms-win-crt-*`), which is an operating-system component from **Windows 10**
  onwards, so Windows 10 or later is the floor. Every build asserts this by reading the
  executable's import table (`scripts/pe_deps.py`).
- **Linux:** the AppImage and the `.tar.gz` both find their assets from any working directory.
- **macOS:** arm64 (Apple silicon) only for this release.

## Verification

Built, launched and smoke-tested on the release runners (headless 600-frame runs from a foreign
working directory, plus dependency audits on each platform). Unit tests, sanitizers, shader
profile compilation and an automated browser playtest are green in CI.

**Performance was not measured on real hardware.** Every rendering number in this project comes
from software rendering (SwiftShader / Mesa llvmpipe) or from the Noop backend.

## Known limitations

- Unsigned binaries, as above.
- Distant-tree impostors, distant mountain silhouettes, water refraction, shore foam, campfire
  smoke, heat shimmer and TAA are **deferred** to a later release.
- The single-file web build keeps saves in `localStorage`; opening it straight from `file://`
  means persistence depends on the browser allowing storage for that origin. The game keeps
  running if it is unavailable.
- A leg-motion discontinuity of at most 0.11 m can occur when a walking character changes
  gesture or crouch state — below the 0.15 m guard, and not a foot-sliding or IK failure.

## Licences

Ships no third-party art or audio: every visual and audible asset is generated in code from a
fixed seed. UI fonts are Source Serif 4 (SIL OFL-1.1). Rendering uses bgfx (BSD-2-Clause), input
and audio use SDL 3 (zlib), the web build uses Emscripten (MIT / NCSA).
