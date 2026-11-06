// miniflac (0BSD, vendor/miniflac) compiled as its own C unit: the header is
// written for C, allocates nothing and needs no libc. Asserts are disabled so
// a corrupt stream returns an error code instead of aborting the firmware.
#if !defined(CYD_UPLOADER) && defined(CYDPOD_FLAC_LEGACY)
#define NDEBUG
// Arduino uses -Os globally. Keep O2 local to this C unit (including all
// upstream decoder functions); vendor sources remain byte-identical.
#if defined(__GNUC__)
#pragma GCC optimize ("O2")
#endif
#define MINIFLAC_IMPLEMENTATION
#include "miniflac.h"
#endif
