// CY-5: neutralizes the one piece of vendored M4A state that outlives a reader instance.
// SampleExtractor::currentSampleSize() (M4ACommonDemuxer.h) keeps `static size_t last_index/last_size` shared by every instance and never
// reset by begin(). A track's first lookup is index 0; if the previous track's last lookup was also index 0 (a 1-frame table, or playback
// aborted inside frame 0), the lookup returns the previous track's size WITHOUT consuming the new table, and the new track is read
// one entry out of step (reproduced: a 131-frame track plays 1 frame). The statics are unreachable from outside, so this primes them:
// a throwaway extractor looks up index 1, leaving last_index == 1, which no new track's first lookup (index 0) can hit. (The primer
// must not start at index 0 itself: it would take the same stale hit.)
// Cost: one SampleExtractor (1 KiB buffer + a 1-entry table), freed on return. Called from M4aGuardedReader::begin().
#pragma once
#include "AudioTools/AudioCodecs/M4ACommonDemuxer.h"

namespace cydpod {
inline void m4aPrimeSampleSizeCache() {
  struct Primer : audio_tools::M4ACommonDemuxer::SampleExtractor {
    using SampleExtractor::SampleExtractor;
    void prime() {
      sampleIndex = 1;
      p_sample_sizes->resize(1);
      p_sample_sizes->write(1);
      currentSampleSize();  // hit (last_index already 1) or miss (reads the entry): either way last_index == 1 afterwards
    }
  };
  audio_tools::M4ACommonDemuxer::M4AAudioConfig cfg;
  Primer p(cfg);
  p.prime();
}
}  // namespace cydpod
