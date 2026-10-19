#include "../firmware/main/text_layout.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
std::vector<uint8_t> read(const char *p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}
int main(int argc, char **argv) {
  assert(argc == 3);
  auto bytes = read(argv[1]), wire = read(argv[2]);
  rtd::Font f;
  assert(f.open(bytes.data(), bytes.size()));
  rtd::TextDocument doc;
  std::string error;
  assert(rtd::parse_text(wire.data(), wire.size(), doc, error));
  assert(rtd::layout(doc, f, error));
  std::vector<uint32_t> original, rendered;
  for (auto &i : doc.items) {
    auto c = rtd::codes(i.text);
    for (auto x : c)
      if (x != '\n')
        original.push_back(x);
  }
  uint8_t bitmap[rtd::Bytes];
  for (auto &p : doc.pages) {
    assert(rtd::render(p, f, bitmap));
    for (auto &l : p.lines) {
      assert(l.y >= 50 && l.y + 32 <= 364);
      if (l.x != 12)
        for (auto c : l.chars)
          rendered.push_back(c);
    }
  }
  assert(original == rendered);
  assert(doc.pages.size() > 3);
  auto bad = wire;
  bad.push_back(0);
  assert(!rtd::parse_text(bad.data(), bad.size(), doc, error));
  std::vector<uint32_t> c;
  assert(!rtd::utf8("\xc0\x80", c));
  assert(!rtd::utf8("\xed\xa0\x80", c));
  assert(rtd::parse_text(wire.data(), wire.size(), doc, error));
  doc.items[0].text = "\xf0\x9f\xa6\x84";
  assert(!rtd::layout(doc, f, error));
  std::cout << "Text layout: exact characters, bounds, UTF-8 and unknown glyph "
               "checks passed\n";
}
