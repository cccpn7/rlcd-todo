"""RTD4 text wire format. No font rasterizer or device transport dependency."""

import struct

CATEGORIES = ("focus", "misc", "follow")
LIMIT = 128 * 1024


def build_text_snapshot(tasks, version, timestamp, supported=None):
    visible = [t for t in tasks if not t.get("done") and not t.get("removed")]
    visible.sort(key=lambda t: (CATEGORIES.index(t["category"]), t["position"], t["id"]))
    if len(visible) > 1024:
        raise ValueError("事项数量超过设备上限")
    body = bytearray(struct.pack("<4sQQHH", b"RTD4", version, timestamp, len(visible), 1))
    for t in visible:
        raw, ident = t["text"].encode(), t["id"].encode()
        if not 0 < len(raw) <= 20480 or not 0 < len(ident) <= 256 or "\0" in t["text"]:
            raise ValueError("事项内容长度或字符无效")
        if supported is not None:
            missing = sorted({ord(c) for c in t["text"] if c != "\n"} - supported)
            if missing:
                raise ValueError("字库不支持：" + ", ".join(f"U+{c:04X}" for c in missing[:10]))
        body += struct.pack("<BHI", CATEGORIES.index(t["category"]), len(ident), len(raw))
        body += ident + raw
    if len(body) > LIMIT:
        raise ValueError("完整清单超过128KiB，请减少内容后重新确认")
    return bytes(body)
