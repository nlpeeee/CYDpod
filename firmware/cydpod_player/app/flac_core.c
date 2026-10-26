// miniflac (0BSD, vendor/miniflac) compiled as its own C unit: the header is
// written for C, allocates nothing and needs no libc. Asserts are disabled so
// a corrupt stream returns an error code instead of aborting the firmware.
#ifndef CYD_UPLOADER
#define NDEBUG
#define MINIFLAC_IMPLEMENTATION
#include "miniflac.h"
#endif
