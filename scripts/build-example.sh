#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/env.sh"
if [[ -z "${RLCD_EXAMPLE_DIR:-}" || ! -f "$RLCD_EXAMPLE_DIR/CMakeLists.txt" ]]; then
    echo "Set RLCD_EXAMPLE_DIR to the prepared official example; see docs/development.md." >&2
    exit 1
fi
exec "$SCRIPT_DIR/idf.sh" -C "$RLCD_EXAMPLE_DIR" build
