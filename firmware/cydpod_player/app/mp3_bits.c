// Select the pinned core's existing MPEG-2.5 (11-bit sync) gate without
// changing Helix sources. Private names avoid Arduino's direct object linkage;
// --wrap redirects only the stock frame-header parser to this equivalent gate.
#ifndef CYD_UPLOADER
#include <libhelix-mp3/coder.h>
#undef SYNCWORDL
#define SYNCWORDL 0xe0
#undef SetBitstreamPointer
#define SetBitstreamPointer cyd_mp3_SetBitstreamPointer
#undef GetBits
#define GetBits cyd_mp3_GetBits
#undef CalcBitsUsed
#define CalcBitsUsed cyd_mp3_CalcBitsUsed
#undef CheckPadBit
#define CheckPadBit cyd_mp3_CheckPadBit
#undef UnpackFrameHeader
#define UnpackFrameHeader cyd_mp3_UnpackFrameHeader
#undef UnpackSideInfo
#define UnpackSideInfo cyd_mp3_UnpackSideInfo
#include <libhelix-mp3/bitstream.c>
int __wrap_xmp3_UnpackFrameHeader(MP3DecInfo* decoder,unsigned char* bytes) {
  return cyd_mp3_UnpackFrameHeader(decoder,bytes);
}
#endif
