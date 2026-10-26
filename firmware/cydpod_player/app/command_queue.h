#pragma once
#include <stdint.h>
// Caller holds its cross-task lock. No allocation and no silent overwrites.
namespace cyd {
template<unsigned N> struct CommandQueue {
  uint8_t keys[N]{}; unsigned head=0, count=0;
  bool push(uint8_t key){if(count==N)return false;keys[(head+count)%N]=key;++count;return true;}
  bool pop(uint8_t& key){if(!count)return false;key=keys[head];head=(head+1)%N;--count;return true;}
};
inline bool currentCompletion(uint32_t completed,uint32_t requested){return completed==requested;}
}
