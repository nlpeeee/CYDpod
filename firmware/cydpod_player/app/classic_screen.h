#pragma once
void classic_return();
void classic_home();

#include <stdint.h>
#include "touch_gesture.h"
TouchListMode classic_touchMode();
void classic_touchPollComplete(uint32_t now);
// Newest list release (CY-105 section 4 / CY-108): one `stat: scroll` line per release carries these.
struct ClassicScrollStats { uint32_t frames,lastFrameMs,maxFrameMs,claimToPaintMs,flings,underruns;
  float releaseVelocity; uint32_t releaseGapMs,releasePauseMs,releaseSamples;
  uint16_t id,pollGapMax,changedGapMax,resets,n,spanMs,stillMs; int16_t rawDy,filtDy;  // gesture id, sample gaps (ms), gap restarts, estimator run, raw/filtered travel
  float vEst,coastRows,remRows; const char* reject="none"; bool aborted;
  uint32_t pollGaps,pollOver50,frameGaps,frameOver50,titleRows,titleMisses;
  int32_t coastPaintMs=-1; };          // px/s before the gates, planned/remaining coast rows, why no coast
ClassicScrollStats classic_scrollStats();
void classic_logScrollStats();
bool classic_command(const char* line);  // `scroll ...` debug knobs; false when the line is something else
