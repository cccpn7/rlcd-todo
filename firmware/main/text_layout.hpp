#pragma once
#include "core.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace rtd {
struct Glyph {
  uint16_t w, h;
  int16_t x, y;
  uint32_t advance;
  const uint8_t *bits;
};
class Font {
  const uint8_t *p = nullptr;
  size_t size = 0;
  uint32_t count = 0, base = 0;

public:
  bool open(const uint8_t *data, size_t n) {
    if (n < 16 || memcmp(data, "RLF1", 4))
      return false;
    auto c = u32(data + 4), b = u32(data + 8), len = u32(data + 12);
    if (c > 100000 || b != 16 + c * 24 || b > n || len > n - b)
      return false;
    uint32_t prior = 0;
    for (uint32_t i = 0; i < c; i++) {
      auto r = data + 16 + i * 24;
      auto key = u32(r), off = u32(r + 4);
      size_t bytes = (size_t(u16(r + 8)) * u16(r + 10) + 7) / 8;
      if ((i && key <= prior) || off > len || bytes > len - off ||
          u32(r + 16) > 65536)
        return false;
      prior = key;
    }
    p = data;
    size = n;
    count = c;
    base = b;
    return true;
  }
  bool get(uint32_t cp, unsigned px, Glyph &g) const {
    if (!p || cp > 0x10ffff)
      return false;
    uint32_t key = (px << 24) | cp, l = 0, h = count;
    while (l < h) {
      auto m = (l + h) / 2;
      if (u32(p + 16 + m * 24) < key)
        l = m + 1;
      else
        h = m;
    }
    if (l == count)
      return false;
    auto r = p + 16 + l * 24;
    if (u32(r) != key)
      return false;
    g = {u16(r + 8),           u16(r + 10), int16_t(u16(r + 12)),
         int16_t(u16(r + 14)), u32(r + 16), p + base + u32(r + 4)};
    return true;
  }
};
inline bool utf8(const std::string &s, std::vector<uint32_t> &out) {
  out.clear();
  for (size_t i = 0; i < s.size();) {
    uint8_t c = s[i++];
    uint32_t cp;
    unsigned more;
    if (c < 128) {
      cp = c;
      more = 0;
    } else if (c >= 0xc2 && c <= 0xdf) {
      cp = c & 31;
      more = 1;
    } else if (c >= 0xe0 && c <= 0xef) {
      cp = c & 15;
      more = 2;
    } else if (c >= 0xf0 && c <= 0xf4) {
      cp = c & 7;
      more = 3;
    } else
      return false;
    if (i + more > s.size())
      return false;
    for (unsigned j = 0; j < more; j++) {
      auto b = uint8_t(s[i++]);
      if ((b & 0xc0) != 0x80)
        return false;
      cp = (cp << 6) | (b & 63);
    }
    if ((more == 1 && cp < 128) || (more == 2 && cp < 2048) ||
        (more == 3 && cp < 65536) || cp > 0x10ffff ||
        (cp >= 0xd800 && cp <= 0xdfff) || cp == 0)
      return false;
    out.push_back(cp);
  }
  return true;
}
struct TextItem {
  uint8_t category;
  std::string id, text;
};
struct TextLine {
  int x, y;
  std::vector<uint32_t> chars;
};
struct TextPage {
  uint8_t category;
  unsigned index = 0, count = 0;
  std::vector<TextLine> lines;
};
struct TextDocument {
  uint64_t version = 0, stamp = 0;
  std::vector<TextItem> items;
  std::vector<TextPage> pages;
};
inline std::vector<uint32_t> codes(const std::string &s) {
  std::vector<uint32_t> c;
  utf8(s, c);
  return c;
}
inline int width(const Font &font, const std::vector<uint32_t> &s,
                 unsigned px = 24) {
  uint32_t n = 0;
  for (auto c : s) {
    Glyph g;
    if (!font.get(c, px, g))
      return -1;
    n += g.advance;
  }
  return (n + 63) / 64;
}
inline bool parse_text(const uint8_t *p, size_t n, TextDocument &doc,
                       std::string &error) {
  auto fail = [&](const char *s) {
    error = s;
    return false;
  };
  doc = {};
  if (n < 24 || n > Limit || memcmp(p, "RTD4", 4) || u16(p + 22) != 1)
    return fail("Invalid text format");
  doc.version = u64(p + 4);
  doc.stamp = u64(p + 12);
  unsigned count = u16(p + 20);
  if (!doc.version || doc.version > 9007199254740991ULL || count > 1024)
    return fail("Invalid text version/count");
  size_t pos = 24;
  int prior = -1;
  std::vector<std::string> ids;
  for (unsigned i = 0; i < count; i++) {
    if (pos + 7 > n)
      return fail("Truncated text item");
    unsigned cat = p[pos], idlen = u16(p + pos + 1), len = u32(p + pos + 3);
    pos += 7;
    if (cat > 2 || int(cat) < prior || !idlen || idlen > 256 || !len ||
        len > 20480 || idlen + len > n - pos)
      return fail("Invalid text item");
    TextItem item{uint8_t(cat), std::string((char *)p + pos, idlen),
                  std::string((char *)p + pos + idlen, len)};
    std::vector<uint32_t> check;
    if (!utf8(item.text, check) || !utf8(item.id, check) ||
        std::find(ids.begin(), ids.end(), item.id) != ids.end())
      return fail("Invalid UTF-8 or duplicate id");
    ids.push_back(item.id);
    doc.items.push_back(std::move(item));
    prior = cat;
    pos += idlen + len;
  }
  return pos == n || fail("Trailing snapshot bytes");
}
inline bool layout(TextDocument &doc, const Font &font, std::string &error) {
  doc.pages.clear();
  for (unsigned cat = 0; cat < 3; cat++) {
    size_t start = doc.pages.size();
    doc.pages.push_back({uint8_t(cat), 0, 0, {}});
    int y = 50;
    unsigned number = 0;
    for (const auto &item : doc.items) {
      if (item.category != cat)
        continue;
      number++;
      auto prefix = codes(std::to_string(number) + "续 ");
      int x = 12 + std::max(22, width(font, prefix) + 2);
      if (x > 110) {
        error = "Too many items";
        return false;
      }
      std::vector<uint32_t> chars;
      if (!utf8(item.text, chars)) {
        error = "Invalid UTF-8";
        return false;
      }
      std::vector<std::vector<uint32_t>> lines(1);
      uint32_t advance = 0;
      for (auto c : chars) {
        if (c == '\n') {
          lines.push_back({});
          advance = 0;
          continue;
        }
        Glyph g;
        if (!font.get(c, 24, g)) {
          error = "Unsupported character U+";
          char s[16];
          snprintf(s, sizeof(s), "%04lX", (unsigned long)c);
          error += s;
          return false;
        }
        if (g.h > 32 || g.w > unsigned(288 - x)) {
          error = "Glyph exceeds line bounds";
          return false;
        }
        if (!lines.back().empty() &&
            advance + g.advance > unsigned((288 - x) * 64)) {
          lines.push_back({});
          advance = 0;
        }
        lines.back().push_back(c);
        advance += g.advance;
      }
      if (lines.size() * 32 <= 314 && y + lines.size() * 32 > 364) {
        doc.pages.push_back({uint8_t(cat), 0, 0, {}});
        y = 50;
      }
      bool continued = false;
      size_t cursor = 0;
      while (cursor < lines.size()) {
        if (y + 32 > 364) {
          doc.pages.push_back({uint8_t(cat), 0, 0, {}});
          y = 50;
        }
        auto label = codes(std::to_string(number) + (continued ? "续" : "."));
        doc.pages.back().lines.push_back({12, y, label});
        while (cursor < lines.size() && y + 32 <= 364) {
          doc.pages.back().lines.push_back({x, y, lines[cursor++]});
          y += 32;
        }
        if (cursor < lines.size()) {
          continued = true;
          doc.pages.push_back({uint8_t(cat), 0, 0, {}});
          y = 50;
        } else
          y += 6;
        if (doc.pages.size() > 128) {
          error = "More than 128 pages";
          return false;
        }
      }
    }
    auto count = doc.pages.size() - start;
    for (size_t i = start; i < doc.pages.size(); i++) {
      doc.pages[i].index = i - start + 1;
      doc.pages[i].count = count;
    }
  }
  if (doc.pages.size() > 128) {
    error = "More than 128 pages";
    return false;
  }
  return true;
}
inline void ink(uint8_t *out, int x, int y) {
  if (x >= 0 && x < 300 && y >= 0 && y < 400) {
    auto i = y * 300 + x;
    out[i / 8] &= ~(0x80 >> (i % 8));
  }
}
inline bool draw_text(uint8_t *out, const Font &font,
                      const std::vector<uint32_t> &chars, int x, int y,
                      unsigned px) {
  int top = 0;
  bool first = true;
  for (auto c : chars) {
    Glyph g;
    if (!font.get(c, px, g))
      return false;
    if (g.h && (first || g.y < top)) {
      top = g.y;
      first = false;
    }
  }
  uint32_t advance = 0;
  for (auto c : chars) {
    Glyph g;
    font.get(c, px, g);
    int gx = x + int(advance / 64) + g.x, gy = y + g.y - top;
    if (g.w && g.h && (gx < 0 || gx + g.w > 300 || gy < 0 || gy + g.h > 400))
      return false;
    for (unsigned yy = 0; yy < g.h; yy++)
      for (unsigned xx = 0; xx < g.w; xx++) {
        size_t i = yy * g.w + xx;
        if (g.bits[i / 8] & (0x80 >> (i % 8)))
          ink(out, gx + xx, gy + yy);
      }
    advance += g.advance;
  }
  return true;
}
inline bool render(const TextPage &page, const Font &font, uint8_t *out) {
  memset(out, 255, Bytes);
  const char *titles[] = {"今日重点", "今日杂项", "今日追踪"};
  if (!draw_text(out, font, codes(titles[page.category]), 12, 7, 26))
    return false;
  for (int x = 12; x < 288; x++) {
    ink(out, x, 38);
    ink(out, x, 373);
  }
  if (page.lines.empty() &&
      !draw_text(out, font, codes("暂无事项"), 102, 194, 24))
    return false;
  for (auto &line : page.lines)
    if (!draw_text(out, font, line.chars, line.x, line.y, 24))
      return false;
  return draw_text(
      out, font,
      codes(std::to_string(page.index) + "/" + std::to_string(page.count)), 12,
      381, 12);
}
} // namespace rtd
