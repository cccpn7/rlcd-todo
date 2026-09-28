"""One layout engine for browser previews and the device's cached 1-bit pages."""

import base64
import struct
import zlib
from dataclasses import dataclass
from functools import lru_cache
from io import BytesIO
from pathlib import Path

from fontTools.ttLib import TTFont
from PIL import Image, ImageDraw, ImageFont

FONT = Path(__file__).parent / "assets/SourceHanSansCN-Regular.otf"
CATEGORIES = {"focus": "今日重点", "misc": "今日杂项", "follow": "今日追踪"}
WIDTH, HEIGHT, LIMIT = 300, 400, 128 * 1024
TOP, BOTTOM, LINE, GAP = 44, 364, 24, 6


@lru_cache
def font(size=18):
    return ImageFont.truetype(str(FONT), size)


@lru_cache
def supported():
    with TTFont(FONT) as f:
        return frozenset(f.getBestCmap())


def validate_text(text):
    missing = sorted({c for c in text if c != "\n" and ord(c) not in supported()})
    if missing:
        raise ValueError("字体暂不支持这些字符：" + " ".join(repr(c) for c in missing[:20]))


def wrap(text, width):
    """Preserve every character; explicit newlines also reserve a line."""
    result = []
    for paragraph in text.split("\n"):
        line = ""
        for char in paragraph:
            if line and font().getlength(line + char) > width:
                result.append(line)
                line = ""
            line += char
        result.append(line)
    return result


@dataclass
class Page:
    category: str
    entries: list  # (number, continuation, lines, y, text_x)
    index: int = 1
    count: int = 1


def paginate(tasks):
    pages = []
    for category in CATEGORIES:
        items = [t for t in tasks if t["category"] == category and not t["done"]]
        items.sort(key=lambda t: (t["position"], t["id"]))
        group = [Page(category, [])]
        y = TOP
        for number, item in enumerate(items, 1):
            validate_text(item["text"])
            # Always reserve the continuation marker width, keeping line breaks stable.
            text_x = 12 + max(22, int(font().getlength(f"{number}续 ")) + 2)
            if text_x > 110:
                raise ValueError("事项数量过多")
            lines = wrap(item["text"], 288 - text_x)
            if len(lines) * LINE <= BOTTOM - TOP and y + len(lines) * LINE > BOTTOM:
                group.append(Page(category, []))
                y = TOP
            continuation = False
            while lines:
                capacity = (BOTTOM - y) // LINE
                if capacity < 1:
                    group.append(Page(category, []))
                    y = TOP
                    capacity = (BOTTOM - TOP) // LINE
                fragment, lines = lines[:capacity], lines[capacity:]
                group[-1].entries.append((number, continuation, fragment, y, text_x))
                y += len(fragment) * LINE + GAP
                if lines:
                    group.append(Page(category, []))
                    y = TOP
                    continuation = True
        for i, page in enumerate(group, 1):
            page.index, page.count = i, len(group)
        pages.extend(group)
    return pages


def page_image(page):
    im = Image.new("L", (WIDTH, HEIGHT), 255)
    d = ImageDraw.Draw(im)
    d.text(
        (12, 7),
        CATEGORIES[page.category],
        font=font(),
        fill=0,
        anchor="lt",
        stroke_width=0.25,
    )
    d.line((12, 32, 287, 32), fill=0)
    d.line((12, 373, 287, 373), fill=0)
    if not page.entries:
        d.text((150, 194), "暂无事项", font=font(), fill=0, anchor="mt")
    for number, continuation, lines, y, x in page.entries:
        d.text(
            (12, y),
            f"{number}{'续' if continuation else '.'}",
            font=font(),
            fill=0,
            anchor="lt",
        )
        for line in lines:
            d.text((x, y), line, font=font(), fill=0, anchor="lt")
            y += LINE
    d.text((12, 381), f"{page.index}/{page.count}", font=font(12), fill=0, anchor="lt")
    # Deliberately no dithering: hardware is strictly black/white.
    return im.point(lambda value: 255 if value >= 160 else 0, mode="1")


def flat_bits(im):
    # 300 pixels is not byte-aligned. Never use Pillow's row-padded bytes here.
    pixels = list(im.get_flattened_data())
    return bytes(
        sum((1 if pixels[i + j] else 0) << (7 - j) for j in range(8))
        for i in range(0, WIDTH * HEIGHT, 8)
    )


def pack(data):
    """PackBits-like: high bit = repeated byte; low 7 bits = length minus one."""
    out = bytearray()
    i = 0
    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            out.extend((0x80 | (run - 1), data[i]))
            i += run
        else:
            start = i
            i += run
            while i < len(data) and i - start < 128:
                if i + 2 < len(data) and data[i] == data[i + 1] == data[i + 2]:
                    break
                i += 1
            out.append(i - start - 1)
            out.extend(data[start:i])
    return bytes(out)


def unpack(data):
    out = bytearray()
    i = 0
    while i < len(data):
        token = data[i]
        i += 1
        n = (token & 127) + 1
        if token & 128:
            out.extend(bytes([data[i]]) * n)
            i += 1
        else:
            out.extend(data[i : i + n])
            i += n
    if len(out) != 15000:
        raise ValueError("Invalid page")
    return bytes(out)


def build_snapshot(tasks, version, timestamp):
    pages = paginate(tasks)
    if len(pages) > 128:
        raise ValueError("超过 128 个屏幕页，请减少内容后再发布；草稿已保留")
    body = bytearray()
    for p in pages:
        raster = flat_bits(page_image(p))
        body.extend(
            struct.pack("<BHHI", list(CATEGORIES).index(p.category), p.index, p.count, len(raster))
        )
        body.extend(raster)
    # Compress across page boundaries so repeated glyphs and headers share a dictionary.
    result = struct.pack("<4sQQH", b"RTD3", version, timestamp, len(pages)) + zlib.compress(
        body, level=9
    )
    if len(result) > LIMIT:
        raise ValueError("完整屏幕快照超过 128KiB，请减少内容后再发布；草稿已保留")
    return result


def previews(tasks):
    result = []
    pages = paginate(tasks)
    if len(pages) > 128:
        raise ValueError("超过 128 个屏幕页，请减少内容；草稿已保留")
    for p in pages:
        out = BytesIO()
        page_image(p).save(out, format="PNG")
        result.append(
            {
                "category": p.category,
                "index": p.index,
                "count": p.count,
                "image": "data:image/png;base64," + base64.b64encode(out.getvalue()).decode(),
            }
        )
    return result
