#!/bin/bash
# Sourced by project entry points. Only source configuration you trust.
RLCD_PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -f "$RLCD_PROJECT_ROOT/.env.local" ]]; then
    set -a
    source "$RLCD_PROJECT_ROOT/.env.local"
    set +a
fi
