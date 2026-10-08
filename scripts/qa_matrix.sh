#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Visual QA matrix (PROMPT §12): capture the fixed-framing screenshots that make up
# the curated release evidence.
#
#   scripts/qa_matrix.sh [build-dir] [out-dir] [quality] [width] [height]
#
# Each scenario is one browser playtest run against the WebAssembly build in
# <build-dir> (default: build/web-release/src/app) using the `qa` script, which
# screenshots the title screen and then the ?qa= scenario framing.
#
# The screenshots are captured under **software rendering** (SwiftShader) unless a
# real GPU is available, and are labelled as such in the output file names.
#
# Requires: node + the tools/browser dependencies (npm ci in tools/browser).
# -----------------------------------------------------------------------------
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="${1:-build/web-release/src/app}"
OUT_DIR="${2:-qa/v0.1.0-matrix}"
QUALITY="${3:-high}"
WIDTH="${4:-1280}"
HEIGHT="${5:-720}"

if [[ ! -f "$BUILD_DIR/index.html" ]]; then
  echo "no web build in '$BUILD_DIR' — build the web-release preset first" >&2
  exit 1
fi
if [[ ! -d tools/browser/node_modules ]]; then
  (cd tools/browser && npm ci --no-audit --no-fund --loglevel=error)
fi

scenarios=(spawn character dusk dawn camp shrine wolves)
mkdir -p "$OUT_DIR"

echo "QA matrix: ${#scenarios[@]} scenarios + title, ${WIDTH}x${HEIGHT}, quality=$QUALITY (software rendering)"
for qa in "${scenarios[@]}"; do
  echo "== ?qa=$qa"
  (cd tools/browser && node playtest.mjs \
      --dir "../../$BUILD_DIR" \
      --out "../../$OUT_DIR/$qa" \
      --query "qa=$qa&hours=9&quality=$QUALITY&play=1" \
      --script qa --width "$WIDTH" --height "$HEIGHT") > "$OUT_DIR/$qa.log" 2>&1 || {
    echo "scenario '$qa' failed:" >&2
    tail -40 "$OUT_DIR/$qa.log" >&2
    exit 1
  }
done

echo
echo "Captured screenshots:"
ls -la "$OUT_DIR"/*/0*.png 2>/dev/null | sed 's/^/  /'
echo
echo "Curate them into docs/qa/ as v0.1.0-<scenario>-<quality>-swiftshader.png"
