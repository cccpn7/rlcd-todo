#!/usr/bin/env python3
"""Build deterministic 1-bit glyphs; private/generated output stays in local/."""

import hashlib
import json
import struct
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import ImageFont

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "server/assets/SourceHanSansCN-Regular.otf"
OUT = ROOT / "local/device-font"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cmap = TTFont(SOURCE).getBestCmap()
    sets = {12: range(32, 127), 24: sorted(cmap), 26: sorted(map(ord, set("今日重点杂项追踪")))}
    records, pixels = [], bytearray()
    for size, chars in sorted(sets.items()):
        font = ImageFont.truetype(str(SOURCE), size)
        for cp in chars:
            mask, offset = font.getmask2(chr(cp), mode="1", anchor="ls")
            w, h = mask.size
            raw = bytes(mask)
            start = len(pixels)
            for i in range(0, len(raw), 8):
                pixels.append(sum((1 << (7 - j)) for j, v in enumerate(raw[i : i + 8]) if v))
            records.append(
                struct.pack(
                    "<IIHHhhII",
                    (size << 24) | cp,
                    start,
                    w,
                    h,
                    *offset,
                    round(font.getlength(chr(cp)) * 64),
                    0,
                )
            )
    header = struct.pack("<4sIII", b"RLF1", len(records), 16 + 24 * len(records), len(pixels))
    data = header + b"".join(records) + pixels
    assert len(data) <= 4 * 1024 * 1024
    (OUT / "font.bin").write_bytes(data)
    manifest = {
        "format": 1,
        "sha256": hashlib.sha256(data).hexdigest(),
        "bytes": len(data),
        "source_sha256": hashlib.sha256(SOURCE.read_bytes()).hexdigest(),
        "body_codepoints": sorted(cmap),
    }
    (OUT / "manifest.json").write_text(json.dumps(manifest, separators=(",", ":")))
    print(f"Font: {len(records)} glyphs, {len(data)} bytes")


if __name__ == "__main__":
    main()
