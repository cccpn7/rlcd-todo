#!/bin/bash
set -e
source "$(dirname "$0")/env.sh"
if [[ -z "${IDF_PATH:-}" || ! -f "$IDF_PATH/export.sh" ]]; then
    echo "Set IDF_PATH to an ESP-IDF v5.5.2 installation; see docs/development.md." >&2
    exit 1
fi
export IDF_PATH
if [[ -n "${IDF_PYTHON_ENV_PATH:-}" ]]; then
    if [[ ! -x "$IDF_PYTHON_ENV_PATH/bin/python" ]]; then
        echo "IDF_PYTHON_ENV_PATH must contain bin/python." >&2
        exit 1
    fi
    export PATH="$IDF_PYTHON_ENV_PATH/bin:$PATH"
fi
source "$IDF_PATH/export.sh" >/dev/null
exec idf.py "$@"
