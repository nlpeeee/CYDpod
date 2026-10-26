#pragma once
#include <stdint.h>
#include <string.h>
using PcmSink = bool (*)(void*,int16_t,int16_t);
struct FirBank { uint32_t rate, phases, step; const int16_t* coefficients; };
const FirBank* firBank(uint32_t rate);
// Exactly the report's 512 B stereo history + three 32-bit counters.
struct FirState {
  int16_t history[256];
  uint32_t head, phase, wait;
  void reset() { memset(this,0,sizeof(*this));wait=1; }
  bool push(int16_t l,int16_t r,const FirBank& bank,PcmSink sink,void* user);
  int16_t filter(const int16_t* coefficients,unsigned channel) const;
};
static_assert(sizeof(FirState)==524,"FIR history budget");
