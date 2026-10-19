#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
# Build in an ASCII-only mirror: ESP-IDF's generated CMake commands are path-sensitive.
RLCD_BUILD_SOURCE="${RLCD_BUILD_SOURCE:-$HOME/esp/rlcd-todo/application}"
mkdir -p "$RLCD_BUILD_SOURCE"
rsync -a --delete --exclude=build --exclude=managed_components --exclude=sdkconfig --exclude=sdkconfig.old "$RLCD_PROJECT_ROOT/firmware/" "$RLCD_BUILD_SOURCE/"
"$RLCD_PROJECT_ROOT/local/venv/bin/python" "$RLCD_PROJECT_ROOT/scripts/build-device-font.py"
mkdir -p "$RLCD_BUILD_SOURCE/assets"
cp "$RLCD_PROJECT_ROOT/local/device-font/font.bin" "$RLCD_BUILD_SOURCE/assets/font.bin"
exec "$RLCD_PROJECT_ROOT/scripts/idf.sh" -C "$RLCD_BUILD_SOURCE" build
