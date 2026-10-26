// Adapter for the pinned core's disabled MPEG-2.5 gate. coder.h supports
// MPEG25 tables but defaults SYNCWORDL to 0xf0 (12 bits). Select the existing
// 11-bit option without editing any Helix source. This TU provides bitstream.o;
// the archive's duplicate member is consequently not pulled into the image.
#ifndef CYD_UPLOADER
#include <libhelix-mp3/coder.h>
#undef SYNCWORDL
#define SYNCWORDL 0xe0
#include <libhelix-mp3/bitstream.c>
#endif
