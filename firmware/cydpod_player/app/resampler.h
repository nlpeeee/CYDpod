#pragma once
#include <stdint.h>
#include <string.h>
using PcmSink = bool (*)(void*,int16_t,int16_t);
struct FirBank { uint32_t rate, phases, step; const int16_t* coefficients; };
const FirBank* firBank(uint32_t rate);
// Mirrored stereo ring: 32-frame history, contiguous reads without wrap branches.
// Exact rational phase avoids long-track drift; 8 high fractional bits select Q15.
struct FirState {
  int16_t history[128];
  uint32_t head, phase, wait, tail;
  void reset() { memset(this,0,sizeof(*this));wait=17; }
  bool push(int16_t l,int16_t r,const FirBank& bank,PcmSink sink,void* user);
  bool drain(const FirBank& bank,PcmSink sink,void* user);
  int16_t filter(const int16_t* coefficients,unsigned channel) const;
};
static_assert(sizeof(FirState)==272,"polyphase state budget");
