#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
exec "$RLCD_PROJECT_ROOT/local/venv/bin/python" "$RLCD_PROJECT_ROOT/scripts/service.py" "${@:-start}"
