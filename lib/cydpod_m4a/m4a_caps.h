// Allocation caps for the (unmodified) AudioTools M4A reader. Sized from the 25 measured real files, see docs/research/cy3-caps.md.
#pragma once
#include <cstddef>
#include <cstdint>

namespace cydpod {
constexpr size_t kM4aMaxStsdBytes = 1024;       // the only metadata box the reader buffers (onStsd resizes from its declared size); the reader's 2 KiB parse buffer must hold it whole
constexpr size_t kM4aMaxFrameBytes = 2048;      // AAC frame including the 7-byte ADTS header the reader prepends
constexpr size_t kM4aAdtsHeaderBytes = 7;
// stsz entries accepted when the table is file-backed (CY-11a): 2^19 entries = 524,288 frames = 3.38 h at 44.1 kHz / 1024 samples per
// frame; the table stays in the file (2 MiB at most), RAM holds only kM4aWindowEntries. Longer files are refused, never truncated.
constexpr size_t kM4aMaxTableEntries = 524288;
constexpr size_t kM4aWindowEntries = 256;       // raw stsz entries (4 bytes each) cached in RAM = 1,024 B
constexpr size_t kM4aWindowBytes = kM4aWindowEntries * 4;
// Resident fallback (no source attached; host comparisons and the not-yet-wired firmware): 2 bytes each = 32 KiB heap (reader's
// stsz_sample_size_t is uint16_t). Counts above this come back as tableOverflow.
constexpr size_t kM4aResidentEntries = 16384;
constexpr size_t kM4aTableBytes = kM4aResidentEntries * 2;  // largest single allocation request of the resident path
constexpr unsigned kM4aMaxBoxDepth = 8;         // nested container boxes (real files: 5)
constexpr size_t kM4aHeapBudgetBytes = 48 * 1024;  // peak live heap the reader may use with all caps at their maximum
}  // namespace cydpod
