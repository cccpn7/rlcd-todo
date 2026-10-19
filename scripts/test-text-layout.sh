#!/bin/bash
set -euo pipefail
source "$(dirname "$0")/env.sh"
cd "$RLCD_PROJECT_ROOT"
local/venv/bin/python scripts/build-device-font.py
local/venv/bin/python - <<'PY'
from server.text_snapshot import build_text_snapshot
from pathlib import Path
items=[dict(id=str(i),text=('核对中文、English 123 与常用标点。\n完整保留长内容。'*30),category=category,done=False,position=0) for i,category in enumerate(('focus','misc','follow'))]
Path('local/layout-fixture.bin').write_bytes(build_text_snapshot(items,1,1704067201))
PY
"${CXX:-c++}" -std=c++17 -fsanitize=undefined,bounds tests/text_layout_test.cpp -lz -o local/text-layout-test
local/text-layout-test local/device-font/font.bin local/layout-fixture.bin
