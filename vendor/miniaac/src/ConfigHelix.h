#pragma once
/* MinimalAAC: intentionally no SBR and no runtime-generated trig tables. */
#if defined(HELIX_FEATURE_AUDIO_CODEC_AAC_SBR) || defined(AAC_ENABLE_SBR)
#error MinimalAAC must be compiled without SBR
#endif
/* Arduino has no per-library CFLAGS hook. Preserve fixed-point wrapping
 * semantics there as well as in the explicit CMake compiler options. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize ("wrapv", "no-strict-aliasing")
#endif
