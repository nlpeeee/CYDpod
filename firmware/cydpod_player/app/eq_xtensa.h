// ===========================================================================
// eq_xtensa.h -- ESP32 (Xtensa LX6) inner loop for one EQ stage
// ===========================================================================
// Bit-exact with cydeq::stageRef(p, n, 2, *c, *h) in eq_core.h: same five
// MULSH products, wrapping 32-bit adds, +2 accumulator bias, symmetric clamp
// to +-(2^28 - 1), then y = (acc << 2) + 2. Include in exactly one translation
// unit. Uses MUL32_HIGH (mulsh), MINMAX (min/max) and the zero-overhead LOOP,
// all present on the ESP32. Placed in IRAM (.iram1.*) so a flash-cache miss
// during an SD read cannot stall the decoder in the middle of a chunk.
//
// void cydeq_stage_xtensa(int32_t* p, uint32_t n, const Stage* c, History* h)
//   p  first sample of one channel, stride 8 bytes (interleaved stereo int32)
//   n  samples; 0 is allowed
//
// Registers (windowed ABI, entry a1, 48):
//   a2 p        a6 b0   a7 b1   a8 b2   a9 -a1   a10 -a2   a15 clamp limit
//   a3 n, then accumulator      a4 c, then product   a5 h (spilled), then x0
//   a11 x1  a12 x2  a13 y1  a14 y2
// Per sample: 21 instructions, no loads besides x0 and no branches.
#pragma once
#include "eq_core.h"

extern "C" void cydeq_stage_xtensa(int32_t* p, uint32_t n, const cydeq::Stage* c, cydeq::History* h);

#if defined(__XTENSA__)
__asm__(
    "  .section .iram1.cydeq_stage_xtensa,\"ax\",@progbits\n"
    "  .global cydeq_stage_xtensa\n"
    "  .type cydeq_stage_xtensa,@function\n"
    "  .align 4\n"
    "cydeq_stage_xtensa:\n"
    "  entry a1, 48\n"               // h spilled at sp+0, below the window save area
    "  beqz  a3, 2f\n"
    "  s32i  a5, a1, 0\n"
    "  l32i  a6, a4, 0\n"
    "  l32i  a7, a4, 4\n"
    "  l32i  a8, a4, 8\n"
    "  l32i  a9, a4, 12\n"
    "  l32i  a10, a4, 16\n"
    "  l32i  a11, a5, 0\n"
    "  l32i  a12, a5, 4\n"
    "  l32i  a13, a5, 8\n"
    "  l32i  a14, a5, 12\n"
    "  movi  a15, -1\n"
    "  srli  a15, a15, 4\n"          // 0x0fffffff = 2^28 - 1
    "  loop  a3, 1f\n"
    "  l32i  a5, a2, 0\n"            // x0
    "  mulsh a3, a7, a11\n"          // b1 * x1
    "  mulsh a4, a8, a12\n"          // b2 * x2
    "  mov   a12, a11\n"             // x2 = x1
    "  add   a3, a3, a4\n"
    "  mulsh a4, a6, a5\n"           // b0 * x0
    "  mov   a11, a5\n"              // x1 = x0
    "  add   a3, a3, a4\n"
    "  mulsh a4, a9, a13\n"          // -a1 * y1
    "  mulsh a5, a10, a14\n"         // -a2 * y2
    "  add   a3, a3, a4\n"
    "  neg   a4, a15\n"
    "  add   a3, a3, a5\n"
    "  addi  a3, a3, 2\n"            // accumulator bias
    "  mov   a14, a13\n"             // y2 = y1
    "  min   a3, a3, a15\n"
    "  max   a3, a3, a4\n"           // per-stage saturation
    "  slli  a13, a3, 2\n"
    "  addi  a13, a13, 2\n"          // y1 = y = (acc << 2) + 2
    "  s32i  a13, a2, 0\n"
    "  addi  a2, a2, 8\n"
    "1:\n"
    "  l32i  a5, a1, 0\n"
    "  s32i  a11, a5, 0\n"
    "  s32i  a12, a5, 4\n"
    "  s32i  a13, a5, 8\n"
    "  s32i  a14, a5, 12\n"
    "2:\n"
    "  retw\n"
    "  .size cydeq_stage_xtensa, . - cydeq_stage_xtensa\n"
    "  .text\n");
#endif
