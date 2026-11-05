#pragma once
void classic_return();
void classic_home();

#include <stdint.h>
struct ClassicScrollStats { uint32_t frames,lastFrameMs,maxFrameMs,claimToPaintMs,flings,underruns; };
ClassicScrollStats classic_scrollStats();
void classic_logScrollStats();
