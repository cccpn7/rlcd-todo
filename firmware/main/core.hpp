#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#ifdef ESP_PLATFORM
#include "miniz.h"
#else
#include <zlib.h>
#endif
namespace rtd {
constexpr size_t Limit = 128 * 1024, Pixels = 300 * 400, Bytes = Pixels / 8;
inline uint16_t u16(const uint8_t *p) { return p[0] | (uint16_t(p[1]) << 8); }
inline uint32_t u32(const uint8_t *p) {
  return u16(p) | (uint32_t(u16(p + 2)) << 16);
}
inline uint64_t u64(const uint8_t *p) {
  return u32(p) | (uint64_t(u32(p + 4)) << 32);
}
struct Page {
  uint8_t category;
  uint16_t index, count;
  size_t offset, size;
};
inline bool inflate_exact(const uint8_t *p, size_t size, uint8_t *out,
                          size_t capacity) {
#ifdef ESP_PLATFORM
  auto decoder = std::make_unique<tinfl_decompressor>();
  tinfl_init(decoder.get());
  size_t input = size, output = capacity;
  auto status = tinfl_decompress(decoder.get(), p, &input, out, out, &output,
                                 TINFL_FLAG_PARSE_ZLIB_HEADER |
                                     TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  return status == TINFL_STATUS_DONE && input == size && output == capacity;
#else
  z_stream stream = {};
  stream.next_in = const_cast<Bytef *>(p);
  stream.avail_in = size;
  stream.next_out = out;
  stream.avail_out = capacity;
  if (inflateInit(&stream) != Z_OK)
    return false;
  int status = inflate(&stream, Z_FINISH);
  bool ok = status == Z_STREAM_END && stream.total_in == size &&
            stream.total_out == capacity;
  inflateEnd(&stream);
  return ok;
#endif
}
inline bool expand(const uint8_t *wire, size_t size,
                   std::vector<uint8_t> &decoded) {
  if (size < 22 || size > Limit)
    return false;
  if (memcmp(wire, "RTD3", 4)) {
    decoded.assign(wire, wire + size);
    return true;
  }
  unsigned count = u16(wire + 20);
  if (count < 3 || count > 128)
    return false;
  size_t raw = count * (Bytes + 9);
  decoded.resize(22 + raw);
  memcpy(decoded.data(), wire, 22);
  decoded[3] = '0';
  return inflate_exact(wire + 22, size - 22, decoded.data() + 22, raw);
}
inline bool unpack(const uint8_t *p, size_t size, uint8_t *out,
                   bool deflate = false) {
  if (deflate) {
    std::vector<uint8_t> scratch;
    if (!out) {
      scratch.resize(Bytes);
      out = scratch.data();
    }
    return inflate_exact(p, size, out, Bytes);
  }
  size_t i = 0, n = 0;
  while (i < size) {
    uint8_t t = p[i++];
    size_t len = (t & 127) + 1;
    if (n + len > Bytes)
      return false;
    if (t & 128) {
      if (i >= size)
        return false;
      if (out)
        memset(out + n, p[i], len);
      i++;
    } else {
      if (i + len > size)
        return false;
      if (out)
        memcpy(out + n, p + i, len);
      i += len;
    }
    n += len;
  }
  return n == Bytes;
}
inline bool parse(const uint8_t *p, size_t size, std::vector<Page> &pages) {
  pages.clear();
  if (size < 22 || size > 22 + 128 * (Bytes + 9) ||
      (memcmp(p, "RTD0", 4) && memcmp(p, "RTD1", 4) && memcmp(p, "RTD2", 4)))
    return false;
  unsigned count = u16(p + 20);
  if (count < 3 || count > 512)
    return false;
  size_t pos = 22;
  int category = -1;
  unsigned expected = 1, group_count = 0;
  for (unsigned i = 0; i < count; i++) {
    if (pos + 9 > size)
      return false;
    Page page{p[pos], u16(p + pos + 1), u16(p + pos + 3), pos + 9,
              u32(p + pos + 5)};
    if (page.category > 2 || page.count == 0 || page.index > page.count ||
        page.offset + page.size > size)
      return false;
    if (page.category != category) {
      if (page.category != category + 1 ||
          (category >= 0 && expected != group_count + 1))
        return false;
      category = page.category;
      expected = 1;
      group_count = page.count;
    }
    if (page.index != expected++ || page.count != group_count ||
        (p[3] == '0'
             ? page.size != Bytes
             : !unpack(p + page.offset, page.size, nullptr, p[3] == '2')))
      return false;
    pages.push_back(page);
    pos = page.offset + page.size;
  }
  return pos == size && category == 2 && expected == group_count + 1;
}
// The hardware stores blocks of four columns by two rows, not row-major bytes.
inline size_t pixel_index(unsigned x, unsigned y) {
  return (y / 2) * 75 + x / 4;
}
inline uint8_t pixel_mask(unsigned x, unsigned y) {
  return uint8_t(1u << (7 - ((x % 4) * 2 + y % 2)));
}
} // namespace rtd
