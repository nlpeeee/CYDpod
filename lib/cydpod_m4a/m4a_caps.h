// Allocation caps for the (unmodified) AudioTools M4A reader. Sized from the 25 measured real files, see docs/research/cy3-caps.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cydpod {
constexpr size_t kM4aMaxStsdBytes = 1024;       // the only metadata box the reader buffers (onStsd resizes from its declared size); the reader's 2 KiB parse buffer must hold it whole
constexpr size_t kM4aMaxFrameBytes = 2048;      // AAC frame including the 7-byte ADTS header the reader prepends
constexpr size_t kM4aAdtsHeaderBytes = 7;
constexpr size_t kM4aMaxTableEntries = 16384;   // stsz entries; 2 bytes each = 32 KiB heap (reader's stsz_sample_size_t is uint16_t)
constexpr size_t kM4aTableBytes = kM4aMaxTableEntries * 2;
constexpr unsigned kM4aMaxBoxDepth = 8;         // nested container boxes (real files: 5)
constexpr size_t kM4aHeapBudgetBytes = 48 * 1024;  // peak live heap the reader may use with all caps at their maximum
}  // namespace cydpod
