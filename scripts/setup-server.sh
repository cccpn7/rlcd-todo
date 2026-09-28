#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
RLCD_PYTHON="${RLCD_PYTHON:-python3.12}"
"$RLCD_PYTHON" -m venv "$RLCD_PROJECT_ROOT/local/venv"
"$RLCD_PROJECT_ROOT/local/venv/bin/pip" install -r "$RLCD_PROJECT_ROOT/server/requirements.txt"
