#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# CI diagnosability helper: summarises a log file as a single GitHub Actions
# error annotation, so a failed step explains itself in the Checks UI, the run
# view and the check-runs annotations API — including environments where the
# raw job log cannot be downloaded.
#
# Strategy: extract the lines that look like errors (with a little context),
# because dependency configure/build logs are dominated by thousands of
# routine probe lines; then append the raw tail so successful steps stay
# visible next to the failures. Falls back to the raw tail when nothing
# matches.
#
# Usage: scripts/ci_annotate_failure.sh <logfile> <title>
# -----------------------------------------------------------------------------
set -euo pipefail
log="${1:?usage: ci_annotate_failure.sh <logfile> <title>}"
title="${2:-step failed}"
python3 - "$log" "$title" <<'PY'
import re
import sys

path, title = sys.argv[1], sys.argv[2]
try:
    lines = open(path, "rb").read().decode("utf-8", "replace").splitlines()
except OSError as e:
    print(f"::error::{title} — could not read {path}: {e}")
    sys.exit(0)

# Lines that almost always indicate a real failure in cmake/ninja/ctest output.
pat = re.compile(
    r"error|Error|ERROR|fatal|FATAL|FAILED|failed|Could NOT find|could not find|"
    r"No such file|not found|No such device|No package|Unable to|denied|"
    r"No supported|unsupported|missing:|Missing|CMake Error|ninja: build stopped|"
    r"undefined reference|cannot find -l|ld returned",
    re.IGNORECASE,
)
# Routine noise to suppress even when it matches (e.g. "Looking for X - not found").
noise = re.compile(r"Looking for .* - not found|^\s*$", re.IGNORECASE)

out = []
ctx = 2
for i, line in enumerate(lines):
    if pat.search(line) and not noise.search(line):
        lo = max(0, i - ctx)
        chunk = [f"> {lines[j]}" if j == i else f"  {lines[j]}" for j in range(lo, min(len(lines), i + ctx + 1))]
        out.extend(chunk)
        out.append("  ...")
        ctx = 0  # avoid re-printing context for consecutive matches

if not out:
    out = ["(no error-looking lines; raw tail follows)"]

out.append("")
out.append(f"(raw tail, last {min(60, len(lines))} of {len(lines)} lines)")
out.extend(lines[-60:])

text = "\n".join(out).strip()
# Keep the annotation within GitHub's per-annotation size budget.
if len(text) > 20000:
    text = text[-20000:]
# Workflow-command escaping: % -> %25, CR -> %0D, LF -> %0A.
text = text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
print(f"::error::{title} — log summary:%0A{text}")
PY
