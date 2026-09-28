#include "../firmware/main/core.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <set>
int main(int argc, char **argv) {
  std::set<size_t> bits;
  for (unsigned y = 0; y < 400; y++)
    for (unsigned x = 0; x < 300; x++) {
      auto point = rtd::rotate180(x, y);
      assert(point.x < 300 && point.y < 400);
      auto original = rtd::rotate180(point.x, point.y);
      assert(original.x == x && original.y == y);
      auto idx = rtd::pixel_index(point.x, point.y);
      auto mask = rtd::pixel_mask(point.x, point.y);
      assert(idx < 15000);
      assert(mask && !(mask & (mask - 1)));
      assert(bits.insert(idx * 256 + mask).second);
    }
  assert(bits.size() == 120000);
  assert(rtd::rotate180(0, 0).x == 299 && rtd::rotate180(0, 0).y == 399);
  assert(rtd::rotate180(299, 0).x == 0 && rtd::rotate180(299, 0).y == 399);
  assert(rtd::rotate180(0, 399).x == 299 && rtd::rotate180(0, 399).y == 0);
  assert(rtd::rotate180(299, 399).x == 0 && rtd::rotate180(299, 399).y == 0);
  assert(rtd::pixel_index(299, 399) == 14999);
  assert(argc == 2);
  std::ifstream f(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
  std::vector<uint8_t> decoded;
  std::vector<rtd::Page> pages;
  assert(rtd::expand(data.data(), data.size(), decoded));
  assert(rtd::parse(decoded.data(), decoded.size(), pages));
  for (auto p : pages)
    assert(p.size == 15000);
  for (size_t i = 0; i < data.size(); i++)
    assert(!rtd::expand(data.data(), i, decoded));
  data.back() ^= 1;
  assert(!rtd::expand(data.data(), data.size(), decoded));
  data.back() ^= 1;
  assert(rtd::expand(data.data(), data.size(), decoded));
  decoded[22] = 2;
  assert(!rtd::parse(decoded.data(), decoded.size(), pages));
}
