#include "../firmware/components/display/mono_portrait.hpp"
#include <array>
#include <cassert>
#include <cstdio>
int main(){
 std::array<uint8_t,15000> source{};
 std::array<uint8_t,15002> target{};target.front()=0x71;target.back()=0x89;
 for(size_t i=0;i<source.size();i++)source[i]=(i*37+i/17)%256;
 rlcd::pack_portrait180(source.data(),target.data()+1);
 assert(target.front()==0x71&&target.back()==0x89);
 // Independent reference: check every pixel, including all four corners and row boundaries.
 for(unsigned y=0;y<400;y++)for(unsigned x=0;x<300;x++){
  unsigned sx=299-x,sy=399-y,i=sy*300+sx;
  bool expected=source[i/8]&(128>>(i%8));
  unsigned output=(y/2)*75+x/4,bit=7-((x%4)*2+y%2);
  bool actual=target[output+1]&(1<<bit);assert(actual==expected);
 }
 puts("Direct monochrome rotation: all 120000 pixels and buffer guards passed");
}
