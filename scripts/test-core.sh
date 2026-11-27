#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RLCD_PROJECT_ROOT"
mkdir -p local
local/venv/bin/python - <<'PY'
from server.render import build_snapshot
from pathlib import Path
payload=build_snapshot([dict(id='demo',text='边界测试。'*120,category='misc',done=False,position=0)],1,1800000000)
Path('local/test-snapshot.bin').write_bytes(payload)
PY
clang++ -std=c++17 -fsanitize=undefined,bounds -g tests/core_test.cpp -lz -o local/core-test
local/core-test local/test-snapshot.bin
clang++ -std=c++17 -fsanitize=undefined,bounds -g tests/auto_pager_test.cpp -o local/auto-pager-test
local/auto-pager-test

clang++ -std=c++17 -fsanitize=undefined,bounds -g tests/aquarium_test.cpp -o local/aquarium-test
local/aquarium-test

clang++ -std=c++17 -fsanitize=undefined,bounds -g tests/mono_portrait_test.cpp -o local/mono-portrait-test
local/mono-portrait-test

# Fishing uses the same native font metrics as the physical plaque.
clang++ -std=c++17 -fsanitize=undefined,bounds -g tests/task_fishing_test.cpp -o local/task-fishing-test
local/task-fishing-test local/device-font/font.bin
