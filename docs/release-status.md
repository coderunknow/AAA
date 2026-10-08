# Mistpine v0.1.0 — release status & handoff

This file is the **live handoff record** for the v0.1.0 release mission (PROMPT.txt).
It is written to be sufficient on its own: if the session is interrupted, the next
agent should be able to read this file, `git log`, and `gh run list` and continue
without re-deriving anything.

Last updated: 2026-10-08 (session branch `arena/7ae554fb-aaa`).

---

## 0. Immutable constraints (never violate)

| Rule | Source |
|---|---|
| **Do NOT merge PR #2 without the owner's explicit, typed approval.** Silence is not approval. | PROMPT.txt §14.3 |
| No release with a missing / unverified / known-broken asset. | PROMPT.txt §1 |
| `DESIGN_LOG.md` is append-only. Entries use `### #N — date — INTENT\|DECISION\|DONE\|CONTRADICTION\|NOTE`. Never rewrite or reorder history. | PROMPT.txt §3 |
| Evidence discipline (§3.4): distinguish *implemented / compiled / linked / built / launched / tested / visually inspected / benchmarked / CI-verified*. Never overclaim. | PROMPT.txt §3.4 |
| The web build is **bgfx WebGL2 / OpenGL ES 3.0**. Never call it "WebGPU". | PROMPT.txt |
| Never fabricate benchmarks or platform verification. | PROMPT.txt |
| Release notes must carry unsigned-build warnings (SmartScreen, Gatekeeper). | PROMPT.txt §16 |

## 1. Sandbox facts (this environment)

* 2 cores, ~3.8 GiB RAM, **no GPU**, no sudo.
* Network **allowed**: `github.com`, `codeload.github.com`, `api.github.com`,
  `registry.npmjs.org`, `pypi.org`, `files.pythonhosted.org`.
* Network **blocked**: `raw.githubusercontent.com`, `objects.githubusercontent.com`,
  `*.github.io`, `storage.googleapis.com`.
  → **Can never download release artifacts or CI artifacts.** Never claim otherwise.
  → Job logs (`results-receiver.actions.githubusercontent.com`) are also blocked, so
  CI failures are diagnosed via check-run annotations:
  `gh api repos/coderunknow/AAA/check-runs/<job-id>/annotations`
  and by making workflow steps self-annotate (`scripts/ci_annotate_failure.sh`).
* `gh workflow run release.yml` → **404** (GitHub only dispatches workflows present on the
  default branch). Release dry runs are triggered by **pushing the `v0.1.0-rc` tag**; the
  `publish` job is guarded by `(inputs.tag || github.ref_name) == 'v0.1.0'`, so an rc tag
  publishes nothing.
* Toolchain and deps live **outside git** and are **wiped when the sandbox recycles**:
  `~/.cache/aaa-toolchain/{env.sh,cmake,ninja,emscripten,...}`,
  `~/.cache/aaa-deps/{bgfx.cmake@f2ea8fb, SDL3@release-3.4.18}`.
  Rebuild with `tools/sandbox/bootstrap_toolchain.sh all`.
  **Source `~/.cache/aaa-toolchain/env.sh` before any cmake/emscripten command.**

## 2. Repo / branch / PR state

* Branch: `arena/7ae554fb-aaa` (session-locked; never use another branch).
* Base commit on `main`: `d1d50c9`.
* HEAD: `e1d027d` — *release: ship the essl shader profile in native builds; package Windows with python*
* Tag `v0.1.0-rc` force-pushed to `e1d027d` (remote confirms `e1d027db…`).
* **PR #2** — https://github.com/coderunknow/AAA/pull/2 — state `OPEN`, `MERGEABLE`
  (`mergeStateStatus: UNSTABLE` simply reflects in-flight checks).
* Once checks are green, the next action is to **ask the owner to approve and merge**,
  then push the real `v0.1.0` tag to publish.

## 3. Run-tracking table (fill as runs finish)

| Run | Workflow | Ref | Commit | Result | Notes |
|---|---|---|---|---|---|
| 37781867117 | ci | branch | 43ad291 | success | |
| 37781909524 | release | v0.1.0-rc | 43ad291 | failure | annotated-shaderc iteration |
| 37787508763 | ci | branch | (single-file fix) | success | |
| 37787537789 | release | v0.1.0-rc | (single-file fix) | failure | single-file not self-contained |
| 37788317009 | ci | branch | 59a4088 | **success** | all 8 jobs green |
| 37788262691 | release | v0.1.0-rc | 59a4088 | failure | macOS shaderc (Dawn tint vs libc++) |
| 37790830883 | ci | branch | 59a4088 | success | |
| 37790842875 | release | v0.1.0-rc | 59a4088 | failure | Windows `zip` exit 127; Linux packaged smoke missing essl |
| **37796737715** | ci | branch | **e1d027d** | **success** | all 8 jobs green, incl. browser playtest |
| **37796725517** | release | v0.1.0-rc | **e1d027d** | **failure** | web ✅ macOS ✅ shader-assets ✅; **Windows** `dumpbin` exit 157; **Linux** AppImage smoke exit 1 (no annotation) |
| **37801626654** | ci | branch | **bae34a6** | **success** | all 8 jobs green |
| **37801753643** | release | v0.1.0-rc | **bae34a6** | **failure** | shader-assets ✅ web ✅ macOS ✅ **Linux ✅ (fixed!)**; Windows ❌ at the new PE audit step |
| next | ci | branch | (round-5 fix) | pending | revalidation after the round-5 parser fix |
| next | release | v0.1.0-rc | (round-5 fix) | pending | confirming dry run |

**Round-4 fix (commit `HEAD~`-successor), both root-caused:**

* **Linux** — the AppImage step did `cd /` then `> smoke-appimage.log`. `/` is not writable by
  the runner user, so the *redirection* failed (empty status 1) before the binary started, and
  the following `cat smoke-appimage.log` aborted the step under the runner's `-e` before the
  annotate helper ran — which is why there was no annotation. The AppImage was never at fault.
  Now: absolute log path, `-e`-safe annotated extraction, payload-binary assertion. Both paths
  simulated locally (exit 0 on success, annotated exit 1 on extract failure).
* **Windows** — `dumpbin /dependents` aborted twice with an opaque MSYS shell status (157),
  producing no output. The *assertion* moved to `scripts/pe_deps.py`, which reads the PE
  import/delay-import directories (the exact data dumpbin prints) with no VS toolchain
  dependency; dumpbin output is still captured in a `continue-on-error` supplementary step, so
  §9.6 is still satisfied. `pe_deps.py --self-test` passes 5/5 on synthetic PE images.
* **Hardened**: Linux "Render verification (xvfb + llvmpipe)" was unannotated and had never
  actually executed (the AppImage step always failed first). Now annotated; `render.log`
  uploaded with the QA artifacts.

Both runs must be green before asking for merge approval.

## 4. What `e1d027d` fixes (root causes of 37790842875)

1. **Windows packaging** — `windows-2022` images have no `zip` binary (exit 127).
   Packaging now uses `python -m shutil.make_archive`-equivalent (`zipfile`) and asserts
   the archive exists. MinGW also links libgcc/libstdc++/winpthread **dynamically**, which
   would break "unzip → run" on a clean machine; CMake now requests static runtime linking.
2. **Linux packaged smoke test** — the packaged `.tar.gz` did not contain the `essl`
   shader profile, so the foreign-CWD smoke test failed. `essl` is now in every native
   shader-profile set that ships.
3. Both packaged smoke tests are `-e`-safe (`|| rc=$?`) and self-annotate.

## 5. Verification evidence already banked

* **Native headless**: builds clean under `-Werror`; **63/63 unit tests pass**;
  CTest **4/4** (unit, smoke_day, smoke_night_camp, smoke_wolves). Includes contact shadows.
* **Shaders**: host shaderc compiles essl 300_es / glsl 440 / spirv / metal
  (20/20 across the 5 touched shaders). dx11 SM5.0 is validated **only** by the Windows CI job.
* **Web release**: `index.wasm` ~2.0 MB, `index.data` ~0.5 MB.
  Single-file HTML **3.45 MB**, launches from `file://` in Chrome: `ok: true`,
  **0 console errors**, autosave works.
* **Browser playtest** (headless Chromium, SwiftShader WebGL2/GLES3): `ok: true`,
  **0 console errors**, 7 screenshots, title→playing confirmed via `window.__mistpineState`.
  Note `rafFpsIdle ≈ 1.09` — SwiftShader software rendering, *not* a GPU measurement.
* **QA matrix** (`scripts/qa_matrix.sh`, per-scenario `scenario:hours` pairs):
  **7/7 scenarios ok, 0 console errors**. Curated to
  `docs/qa/v0.1.0-{title,day-spawn,character,dusk-forest,night-campfire,shrine,night-wolf,misty-dawn}-low-swiftshader.png`
  (960×540, `low` preset, software rendering).
* **SSAO kernel rewrite** verified numerically bit-identical vs the old GLSL `mat3` form
  over 2000 randomised inputs (max diff `0.000e+00`).

## 6. Known blockers / limitations (must be reported, not hidden)

* **Windows dependency audit had to stop using `dumpbin` as the gate.** It aborted twice with
  an opaque MSYS status (157). `scripts/pe_deps.py` now decides pass/fail from the PE import
  table; `dumpbin` output is still collected as evidence. Flagged here because it is a deviation
  in *tooling* (not in coverage) from the literal wording of PROMPT §9.6.
  **Note (round 5):** the first version of `pe_deps.py` had the optional-header offsets wrong by
  4 bytes in both layouts and reported a healthy executable as importing nothing. Fixed; the
  offsets are now pinned by a format-level assertion (`96 + 16*8 == 224`, `112 + 16*8 == 240`)
  and the self-test covers PE32 *and* PE32+ plus three negative cases (13/13 check). The audit
  also now hard-fails if the artifact is not x86-64. Recorded as DESIGN_LOG #18.
* **GitHub Pages cannot be enabled by me.** `gh api -X POST repos/coderunknow/AAA/pages`
  → **403 "Resource not accessible by integration"** (token lacks admin). Owner action.
  Without Pages, the "web build on a static host" acceptance criterion is **not** satisfied
  end-to-end; the Pages workflow job exists and is skipped/deploy-gated.
  Workaround available to the owner: publish the single-file HTML as a release asset
  (it runs from `file://`).
* **I cannot see images.** Nothing is claimed as "visually inspected". Screenshots are
  release evidence for the owner.
* **No real GPU** anywhere in this pipeline — all rendering evidence is SwiftShader /
  llvmpipe / Noop. No fps claim is a GPU benchmark.
* **Runner-only packaging.** Windows `.zip`, macOS arm64 `.zip`, AppImage and `.tar.gz`
  are built and smoke-tested on their runners; I cannot download or execute them.
* **macOS cannot build the pinned shaderc** (Dawn tint vs libc++:
  `multiplanar_external_texture.cc:159: no viable constructor or deduction guide…`).
  Workaround: a Linux `shader-assets` job compiles all profiles and the macOS job
  downloads them (`-DAAA_SKIP_SHADER_COMPILE=ON`).
* **Residual animation artefact** (DESIGN_LOG #14): a leg-motion discontinuity at a walk on
  gesture/crouch state change — measured **≤0.11 m**, inside the 0.15 m guard. Not a NaN,
  not foot sliding, not an IK failure.
* **Deferred** (DESIGN_LOG #15): distant tree impostors, distant mountain silhouettes,
  water refraction, shore foam, campfire smoke, heat shimmer, TAA.

## 7. Required artifacts (PROMPT §2, owner-final)

Pages web build · Windows `.zip` (x64) · Linux AppImage · Linux `.tar.gz` (x86_64) ·
macOS arm64 `.zip` containing `Mistpine.app` · single-file HTML · `SHA256SUMS.txt`.

Release notes **must** state: Windows unsigned → SmartScreen "More info → Run anyway";
macOS ad-hoc signed, **not notarized** → right-click → Open, or
Privacy & Security → Open Anyway.

## 8. Immediate next steps (in order)

1. ~~Confirm CI run **37796737715** is green~~ — **done, all 8 jobs success.**
2. Confirm the round-4 **CI** run is green (the workflow/scripts change is non-functional for
   CI, but §13 requires the *current PR head* to be green).
3. Confirm the round-4 **release dry run** is green for **all 5 build jobs**
   (shader-assets, web, windows, linux, macos); checksums/publish are expected
   to *skip* on an rc tag.
   Watch specifically for Windows `Dependency audit (PE import table)`. If it fails, the
   annotation carries the toolchain (`gcc -dumpmachine`), the image's machine type and the full
   import list. Two known possible outcomes:
   * `machine is i386 (32-bit)` — the runner's MinGW is 32-bit. Real defect against PROMPT 2
     (Windows x64); fix by pinning an x86_64 toolchain in the release workflow.
   * a named forbidden DLL (e.g. `libwinpthread-1.dll`) — `-static` did not cover it; add the
     missing linker flag.

   Linux `Packaged smoke test (AppImage payload)` and `Render verification (xvfb + llvmpipe)`
   both **passed** in `37801753643` and are no longer open risks.
4. If either fails: read annotations
   (`gh api repos/coderunknow/AAA/check-runs/<job-id>/annotations`), fix, commit,
   force-push `v0.1.0-rc`, repeat.
5. Append a DESIGN_LOG entry summarising the verified release state (entry #17 already covers
   round 4).
6. **Ask the owner to approve and merge PR #2.** Do not merge.
7. After merge: `gh release create v0.1.0 --target <merge-sha> --draft` with notes carrying the
   unsigned-build warnings, then `gh workflow run release.yml -f tag=v0.1.0 -f sha=<merge-sha>`
   (this works after merge because the workflow will then exist on the default branch).

## 9. Hard-won operational notes (do not re-learn)

* **Parallel `edit_file` calls on the same file corrupt it** (lost updates; once produced
  `espace aaa` garbage in `src/render/renderer.cpp`). Apply multiple edits to one file with
  a single deterministic Python script using `assert t.count(a) == 1`. Restore damage with
  `git checkout -- <file>`. Always re-grep / `node --check` after editing.
* `CMAKE_FIND_PACKAGE_REDIRECTS_DIR` on the command line has no effect — CMake wipes
  `build/<preset>/CMakeFiles/pkgRedirects` at configure start.
* Emscripten `-sSINGLE_FILE=1` does **not** inline a `--preload-file` bundle.
  Must use `--embed-file`, else `file://` fetch of `index.data` fails.
* `%zu` is not portable to MSVCRT printf (4 sites in `src/app/app.cpp`) — breaks MinGW
  under `-Werror`. Use `%llu` + cast.
* `AAA_SKIP_SHADER_COMPILE` must define `AAA_ASSET_OUT_DIR` and `AAA_SHADER_SRC_DIR`
  **before** its early return, or the font target does `mkdir(/fonts)` → permission denied.
* shaderc: use `//dependents` (MSYS rewrites `/dependents` into a path).
