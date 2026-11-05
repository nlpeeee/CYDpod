#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cydsearch {
// U+00C0..U+017F, one ASCII base per codepoint; spaces preserve symbols.
// constexpr data is read-only flash on ESP32; no runtime table or heap.
static constexpr char latin[] =
    "aaaaaaaceeeeiiiidnooooo ouuuuyts"
    "aaaaaaaceeeeiiiidnooooo ouuuuyty"
    "aaaaaaccccccccddddeeeeeeeeeegggg"
    "gggghhhhiiiiiiiiiiiijjkkklllllll"
    "lllnnnnnnnnnoooooooorrrrrrssssss"
    "ssttttttuuuuuuuuuuuuwwyyyzzzzzzs"
;
static_assert(sizeof(latin)==193,"Latin folding table bounds");

// Writes <= budget bytes (no terminator); caller supplies zeroed destination.
// Invalid bytes are copied singly. Valid UTF-8 and expansions are indivisible.
inline size_t fold(char* out,size_t budget,const char* in,size_t bytes){
  size_t at=0,used=0;
  bool base=false;
  while(at<bytes){
    uint32_t cp=uint8_t(in[at]);
    size_t width=1;
    if(cp>=0xc2&&cp<=0xf4){
      width=cp<0xe0?2:(cp<0xf0?3:4);
      if(width>bytes-at)width=1;
      else {
        for(size_t j=1;j<width;++j)if((uint8_t(in[at+j])&0xc0)!=0x80){width=1;break;}
        if(width>1){
          cp &= width==2?0x1f:(width==3?0x0f:7);
          for(size_t j=1;j<width;++j)cp=(cp<<6)|(uint8_t(in[at+j])&63);
          if((width==3&&cp<0x800)||(width==4&&cp<0x10000)||
             (cp>=0xd800&&cp<=0xdfff)||cp>0x10ffff){width=1;cp=uint8_t(in[at]);}
        }
      }
    }
    if(width==1&&cp>=0x80)cp=0x110000; // Invalid bytes must not enter the Latin table.
    if(cp>=0x300&&cp<=0x36f&&base){at+=width;continue;}
    const char* expansion=cp==0xdf?"ss":(cp==0xc6||cp==0xe6?"ae":
        (cp==0x152||cp==0x153?"oe":(cp==0x132||cp==0x133?"ij":
        (cp==0xde||cp==0xfe?"th":nullptr))));
    const char mapped=cp>='A'&&cp<='Z'?char(cp+32):
        (cp>=0xc0&&cp<=0x17f&&latin[cp-0xc0]!=' '?latin[cp-0xc0]:0);
    if(expansion){
      if(budget-used<2)break;
      out[used++]=expansion[0];out[used++]=expansion[1];
    }else if(mapped){
      if(used==budget)break;
      out[used++]=mapped;
    }else{
      if(width>budget-used)break;
      for(size_t j=0;j<width;++j)out[used++]=in[at+j];
    }
    // Marks following a base stay associated across a run of combining marks.
    // Non-ASCII valid base characters include unsupported scripts and symbols.
    base=(cp>='A'&&cp<='Z')||(cp>='a'&&cp<='z')||
        (width>1&&!(cp>=0x300&&cp<=0x36f));
    at+=width;
  }
  return used;
}
}
