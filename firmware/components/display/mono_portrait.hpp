#pragma once
#include <cstddef>
#include <cstdint>
namespace rlcd {
// 300×400 upright row-major 1bpp -> ST7305 4×2 blocks, rotated 180 degrees.
// Each source nibble is expanded once; no RGB buffer or per-pixel PSRAM LUT.
inline void pack_portrait180(const uint8_t *source,uint8_t *packed) {
  constexpr uint8_t spread[16]={0x00,0x80,0x20,0xa0,0x08,0x88,0x28,0xa8,0x02,0x82,0x22,0xa2,0x0a,0x8a,0x2a,0xaa};
  for(unsigned by=0;by<200;by++)for(unsigned bx=0;bx<75;bx++){
    unsigned x=296-bx*4,upper=(399-by*2)*300+x,lower=upper-300;
    auto a=(source[upper/8]>>(4-upper%8))&15;
    auto b=(source[lower/8]>>(4-lower%8))&15;
    packed[by*75+bx]=spread[a]|(spread[b]>>1);
  }
}
}
