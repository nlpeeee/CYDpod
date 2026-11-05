#pragma once
void classic_return();
void classic_home();

#include <stdint.h>
struct ClassicScrollStats { uint32_t frames,lastFrameMs,maxFrameMs,claimToPaintMs,flings,underruns;
  float releaseVelocity; uint32_t releaseGapMs,releasePauseMs,releaseSamples; };
ClassicScrollStats classic_scrollStats();
void classic_logScrollStats();
