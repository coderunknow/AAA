#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# CI diagnosability helper: prints the tail of a log file as a single GitHub
# Actions error annotation, so a failed step explains itself in the Checks UI,
# the run view and the check-runs annotations API — including environments
# where the raw job log cannot be downloaded.
#
# Usage: scripts/ci_annotate_failure.sh <logfile> <title>
# -----------------------------------------------------------------------------
set -euo pipefail
log="${1:?usage: ci_annotate_failure.sh <logfile> <title>}"
title="${2:-step failed}"
python3 - "$log" "$title" <<'PY'
import sys
path, title = sys.argv[1], sys.argv[2]
try:
    tail = open(path, "rb").read()[-9000:].decode("utf-8", "replace")
except OSError as e:
    tail = f"(could not read {path}: {e})"
# Workflow-command escaping: % -> %25, CR -> %0D, LF -> %0A.
tail = tail.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")
print(f"::error::{title} — log tail:%0A{tail}")
PY
