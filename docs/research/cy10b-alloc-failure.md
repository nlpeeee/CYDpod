# CY-10b: internal allocation failure in the vendored reader (review R2)

Status: **part 3 of 3 done** (CY-10b1: constructor-time allocator hook overlay; CY-10b2: fixed arena, deferred construction, sticky failure; CY-10b3: failure sweep, arena right-sizing to 16,384 B, firmware wiring). R2 is fixed for every path the guard admits. **Open: the wired firmware does not admit an `.m4a` at the measured board state** (47,840 B needed, 45,488 B free after the file is open: 2,352 B short; see "CY-10b3" at the end). The CY-10b2 sections in the middle are kept as history; where they quote sizes (28,672 B arena, first fit, 256-byte slices, 11,520 B bound) CY-10b3 supersedes them.

## Findings (why a hook, and where it has to sit)

- `Vector` selects its allocator per instance (`setAllocator`) and only when `USE_ALLOCATOR` is true; otherwise it uses `new T[]`. The ESP32 platform config defines `USE_ALLOCATOR true`; `AudioToolsConfig.h` defaults it to `false`, so the existing host suites (built without the define) exercise the `new[]` path, not the firmware's. The hook test builds with `-DUSE_ALLOCATOR=true` to match the target.
- `SingleBuffer` stores `Allocator _allocator` **by value** (`Buffers.h`): the allocator passed to it is sliced to the base `Allocator`, and its default constructor uses a plain base `Allocator`. A per-instance allocator therefore cannot reach `SingleBuffer`s (the reader's stsd buffer, frame buffer, parse buffer, sample-size and chunk-offset tables). The only common point is the base class `Allocator::allocate()` / `Allocator::free()`.
- `SampleExtractor` constructs its `SingleBuffer`s and `Vector tmp` eagerly in its constructor, and the parser's `callbacks` / `levelStack` / `containers` vectors are members too, so nothing CYDpod-side can intervene between construction and the first allocation.
- `Allocator::allocate()` calls `stop()` on a null result, and `createArray()` placement-news into the result; a hook that returns null therefore still aborts or crashes. A shared dummy block cannot be returned safely (several allocations overlap within one vendor call before any check).
- Remaining non-hooked allocation sources: `std::function` objects inside the vendor classes. Measured: none occur for the reader on `fixtures/stereo-faststart.m4a` (callbacks are capture-less or fit the small buffer). The count test would catch a regression.

## Chosen mechanism

No existing global setting reaches `SingleBuffer`, so this is a minimal overlay: `patches/audio-tools/0001-allocator-hook.patch` (one file, `Allocator.h`, +33/-2). It adds

```cpp
struct AllocatorHook { void* (*allocate)(size_t, void* ctx); void (*release)(void*, void* ctx); void* ctx; };
void setAllocatorHook(AllocatorHook*);  AllocatorHook* allocatorHook();   // inline, function-local static slot
```

and makes `Allocator::allocate()` use the hook (zero-filling the block, `size 0 -> 1`) and `Allocator::free()` use `hook->release`. Behaviour with no hook installed is the original code path (`do_allocate` / `::free`); the cost is one pointer load and branch per call. Allocators that override `allocate`/`free` (Zephyr `free`) are not hooked; they are not used on this target.

Contract: install the hook **before** constructing the reader and remove it only after destroying it; memory allocated under a hook must be freed under it.

### Overlay diff (patches/audio-tools/0001-allocator-hook.patch)

```diff
--- a/src/AudioTools/CoreAudio/AudioBasic/Collections/Allocator.h
+++ b/src/AudioTools/CoreAudio/AudioBasic/Collections/Allocator.h
@@ -16,6 +16,29 @@
  */
 
 /**
+ * @brief CYDpod overlay: optional constructor-time allocator hook. While a
+ * hook is installed, Allocator::allocate() and Allocator::free() (and so every
+ * allocator derived from Allocator that does not override them, and every
+ * SingleBuffer/Vector with USE_ALLOCATOR) route through it. The hook must hand
+ * out calloc-like memory; allocate() zero-fills it. With no hook installed
+ * (the default) behaviour is unchanged. Installing or removing a hook while
+ * hooked memory is live is the caller's responsibility.
+ * @ingroup memorymgmt
+ */
+struct AllocatorHook {
+  void* (*allocate)(size_t size, void* ctx);
+  void (*release)(void* memory, void* ctx);
+  void* ctx;
+};
+
+inline AllocatorHook*& allocatorHookSlot() {
+  static AllocatorHook* hook = nullptr;
+  return hook;
+}
+inline void setAllocatorHook(AllocatorHook* hook) { allocatorHookSlot() = hook; }
+inline AllocatorHook* allocatorHook() { return allocatorHookSlot(); }
+
+/**
  * @brief Memory allocateator which uses malloc.
  * @ingroup memorymgmt
  * @author Phil Schatzmann
@@ -70,7 +93,14 @@
 
   /// Allocates memory
   virtual void* allocate(size_t size) {
-    void* result = do_allocate(size);
+    AllocatorHook* hook = allocatorHook();
+    void* result = nullptr;
+    if (hook != nullptr) {
+      result = hook->allocate(size == 0 ? 1 : size, hook->ctx);
+      if (result != nullptr) memset(result, 0, size == 0 ? 1 : size);
+    } else {
+      result = do_allocate(size);
+    }
     if (result == nullptr) {
       LOGE("Allocateation failed for %zu bytes", size);
       stop();
@@ -82,7 +112,10 @@
 
   /// frees memory
   virtual void free(void* memory) {
-    if (memory != nullptr) ::free(memory);
+    if (memory == nullptr) return;
+    AllocatorHook* hook = allocatorHook();
+    if (hook != nullptr) hook->release(memory, hook->ctx);
+    else ::free(memory);
   }
 
  protected:
```

### How it is applied (vendor/ and `tools/audiotools-files.sha256.json` untouched)

- Host: `tests/apply_audiotools_overlay.sh <dest>` copies `vendor/arduino-audio-tools/src` and runs `patch -p1`; `tests/build_m4a_hook.sh` builds against `tests/out/m4a_hook/audiotools`.
- Firmware: `tools/check.ps1` verifies the pristine inventory first, then stages `.tools/staging/audiotools-patched/src`, applies the patches with `git apply`, and points the probe's `-I` at it.

## Evidence

- `tests/m4a_hook_test.cpp` / `build_m4a_hook.sh`: constructs (in place), runs and destroys an `M4aGuardedReader` over `fixtures/stereo-faststart.m4a` with a counting hook, against a global `operator new` + `malloc/calloc/realloc/free` counter (the hook's backing store calls the real functions, so the global counter sees only bypasses). Hook-less run: 65 global allocations / 65 frees, 131 frames. Hooked run: **65 hook allocations, 65 frees, live 0, 26,437 bytes, 0 bypassing global allocations or frees**, same 131 frames and 73,337 ADTS bytes. PASS. 
- Unpatched tree, hook unset: `build_m4a_{host,caps,reject,sequence,r1r5,filetable}.sh` all pass unchanged.
- `powershell -File tools/check.ps1` exit 0 with the overlay applied to the probe (`.tools/logs/cy10b1-check.txt`):

| | bytes | static RAM | sha256 |
|---|---|---|---|
| CY-13a probe (before) | 1,224,944 | 61,552 | `002f811ffaedf48cee190f9dfb225ad013f8557a1f1bb687034d620d00a20914` |
| CY-10b1 probe (overlay, hook unset) | 1,225,152 (+208) | 61,560 (+8) | `ed9411e5db76ed99ceca9177b3fb4a2097df573e865ca94e95da0a3999d62746` |

Compile only; no hardware was touched and nothing was flashed.

## CY-10b2: fixed arena, deferred construction, sticky failure (done)

Status: **part 2 of 3 done.** An internal allocation failure of the vendored reader can no longer abort or crash the board on any path the guard admits: the reader makes one fallible allocation up front, serves every vendor allocation from it, and turns exhaustion into `M4aStatus::noMemory`. CY-10b3 still has to run the exhaustive sweep and wire the firmware hook (end of this file).

### Design

- **One up-front arena** (`lib/cydpod_m4a/m4a_arena.h`). `M4aGuardedReader::begin()` acquires `kM4aArenaMainBytes + kM4aArenaReserveBytes` = **20,480 + 8,192 = 28,672 B** with one call to `M4aArenaMemory::acquire` (default `new (std::nothrow)`; the firmware will plug in `heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`; tests fake it). A refusal returns `no_memory` and nothing else is constructed. The block is kept across tracks (a later `begin()` only resets it: no heap churn, and it is the same one admission) and given back by `end()` or the destructor.
- **Allocator over the arena** (`M4aArena`): first fit with block splitting and coalescing on free (a `Vector` grows by allocating the new block before it frees the old one, so real frees are needed), 8-byte headers, 8-byte alignment, foreign or misaligned frees ignored and counted. `reset()` makes the whole main region one free block and clears the reserve and the flag.
- **Deferred construction.** `demux` (`M4AAudioDemuxer`) and `sizes` (`M4aCappedSizes`) are raw storage inside the reader and are constructed with placement new in `begin()`, after the arena exists and the hook is installed, then primed (CY-5) under the same hook. `begin()` still destroys and rebuilds them on every track, so the CY-5 state reset is unchanged; the arena reset happens between the destroy and the rebuild. A reader that is never begun owns no vendor memory.
- **Hook scope.** `Scope` sets the global hook slot to the reader's hook for exactly one call into the vendored code (`begin()`, each `demux.write`, destruction) and restores the previous value afterwards. Why nothing else is routed into the arena: the producer task is the only task that calls the reader; the slot is a plain global written only by `Scope`, i.e. only on the producer task and only while a vendor call is running; the decoder, Bluetooth and the rest of the firmware allocate with `malloc` / `heap_caps` / Helix's own allocator and never run the vendored `Allocator`. This is a design rule, not something the code can enforce: an audio-tools container used on another task during a vendor call would be routed into the arena. The test checks that the slot is empty between calls and that a hook installed by someone else is neither used nor lost. The frame callback and the file source's `readAt()` run inside the scope and must not allocate through audio-tools containers (the firmware ones do not).
- **Exhaustion is never a crash.** `Allocator::allocate()` calls `stop()` only when the hook returns null; callers write through the result unconditionally. Once the hook is installed it never returns null as long as the request fits the reserve: a request the main region cannot serve sets the sticky `exhausted` flag and is served from the **reserve**, a separate region behind the main one, bump-only, never reused and never overlapping a main block (released reserve blocks are no-ops). The reader checks the flag after **every** call into the vendored code (`vendorDone()`: construction, primer, `demux.begin()`, each `demux.write`), maps it to `noMemory`, and every later `write()`/`finish()` returns it again. Vendor state after exhaustion is not meaningful, so the track is over; the next `begin()` resets the arena and plays normally. The one residual is a request beyond the reserve (null, so the vendor's `stop()` runs as before); the per-call bound below excludes it with a 1.7x margin.
- **Bounded calls.** Frame extraction is the only repeating allocator in the vendor (the extractor's frame buffer and ADTS scratch `tmp` grow to every larger frame, and `Vector`/`SingleBuffer` grow by exactly the new size). So that one vendor call cannot request unbounded memory, `write()` hands the **top-level mdat payload** to the vendor in slices of at most `kM4aVendorSliceBytes` = 256 B and checks the flag between slices; everything else (whole headers, the stsd/stsz/mdat zones, other boxes) is delivered in one piece exactly as before. The vendor already accepts arbitrary chunking of mdat payload (the firmware feeds 256-byte chunks).
- **Table mode.** File-backed (the shipping mode) uses no heap for the table. A resident (no source) table in arena mode is limited to `kM4aArenaResidentEntries` = 256 entries (512 B) by `M4aCappedSizes::hardLimit()`, which `residentLimit()` cannot lift; larger counts return `table_overflow` as before.
- **Legacy mode.** Without the overlay (`CYDPOD_ALLOCATOR_HOOK` undefined) or with `setArenaMemory(nullptr)` the vendor allocates from the heap as before (the pristine-tree host suites and the hook test's baseline use this).

### Second overlay change: `patches/audio-tools/0002-allocator-hook-feature-macro.patch`

Two added lines in `Allocator.h`, after `allocatorHook()`:

```diff
+/// Feature marker so CYDpod code can also build against the pristine (hook-less) tree.
+#define CYDPOD_ALLOCATOR_HOOK 1
```

Justification: `m4a_guarded_reader.h` is also compiled against the pristine tree by the existing host suites (they must keep passing unchanged), so it needs a way to know whether `AllocatorHook` exists. No other vendor change is needed: inside the proven bound the hook never returns null, so `stop()` is not reached and the placement-new in `createArray()` always writes into real memory. `vendor/` and `tools/audiotools-files.sha256.json` are untouched.

### Proven bound on live bytes (`kM4aArenaLiveBoundBytes` = 11,520 B)

The guard admits only `moov/trak/mdia/minf/stbl` containers (depth <= 8), one `stsd` (<= 1,024 B), one `stsz`, one `mdat`, and frames of 1..2,041 bytes; every other box is renamed `free` and skipped by the vendor (no buffering, no callback), and `stco` has no vendor handler. The vendor's heap objects are therefore exactly:

| object | live bytes (worst case) | why |
|---|---|---|
| `MP4Parser::buffer` | 2,048 | fixed 2 KiB in `begin()`, never resized; `write()` feeds it in buffer-sized pieces |
| `M4ACommonDemuxer::buffer` | 2,056 | 2,048 default; `onStsd` resizes to the stsd box (<= 1,024: no growth); `onStsz` resizes to leftover (<= 3) + chunk, so at most 2,051 -> 2,056 |
| `parser.callbacks` | 336 | 6 `CallbackEntry` x 56 B (host 64-bit; the 32-bit target is not larger) |
| `parser.containers` | 272 | 17 `ContainerInfo` x 16 B (built once, at the first container check) |
| `parser.levelStack` | 64 | depth <= 8 x 8 B (capacity is kept after pops) |
| `SampleExtractor::buffer` | 2,041 | grows to the current frame size, at most 2,048 - 7 |
| `SampleExtractor::tmp` (ADTS scratch) | 2,048 | frame + 7 |
| predecessor during a growth | 2,047 | the new block is allocated before the old is freed; only one of buffer/tmp grows at a time and the old one is smaller than the new |
| resident sample-size table | 512 | only without a source: <= 256 entries x 2 B (file-backed: 0; the 1,024 B window lives in the reader object) |

Sum 11,424 B; +8 for rounding each block to 8 bytes and + 9 blocks x 8-byte headers (72) = **11,504 B, stated as 11,520**. Transients during construction and `begin()` (callback vector growth 56..336 B, the 1,024 B initial extractor buffer, the primer's 1,024 B buffer and 2 B table) are smaller than this steady state. The bound follows from the caps in `m4a_caps.h` and does not depend on the length or content of the file.

Evidence that it holds (`tests/build_m4a_arena.sh`, measured with the arena's own live counter):

| input | peak live | high water (incl. fragmentation) |
|---|---|---|
| fixture `stereo-faststart.m4a` | 7,544 | 8,056 |
| 25 real files | 8,312 .. 8,824 | <= 12,040 |
| adversarial: frame sizes 1..2041 ascending / descending, 3000 one-byte frames, 40 max-size frames, alternating tiny/max; stsd at its 1,024 B cap; container depth 8; 1,500 random frame-size streams; test chunk sizes 1 B .. 4 MiB | <= 10,984 (bound 11,504) | <= 16,128 |
| every malformed case of the caps suite and the reject suite (105 runs), run through the arena | <= the bound; statuses identical to the heap run | |

The largest observed peak (10,984) is within 5 % of the bound, so the table is tight, not slack.

### Arena size and margin

- **Main region 20,480 B.** First-fit fragmentation is the only reason the arena has to be larger than the 11,520 B live bound: growth sequences leave holes (worst high water seen over the 1,500 random streams and the real files: 16,128 B, 1.47x the live peak). 20,480 B is 1.78x the bound and 1.27x the worst high water seen. Running out of main space is an availability event (clean `no_memory`), never a safety event.
- **Reserve 8,192 B.** Worst request total of one vendor call, by structure: `demux.begin()` (parser 2,048 + demuxer 2,048 with headers = 4,112 B, also the measured maximum over every test; at most 25 requests); the first container check (17 growth steps, 16 x 153 B + headers = 2,584 B) plus the level stack (<= 352 B); one 256-byte payload slice: ADTS scratch growths (<= 2,056 B for a carried-over large frame, otherwise <= 601 B in <= 23 strictly growing steps) plus frame-buffer growths (<= 601 B + 2,056 B for the last, partial frame), about 4.8 KB. 8,192 B is 1.7x that. The forced-exhaustion sweep used at most 4,096 B and 8 reserve requests.
- **Total acquisition 28,672 B**, a fixed cost admitted once per reader (the reader previously used about 8.7 KB of scattered vendor allocations plus the object). Firmware admission (`kReaderPeakBytes` etc.) is CY-10b3.

### Tests

`tests/m4a_arena_test.cpp` / `tests/build_m4a_arena.sh` (WSL; plain and `-fsanitize=address,undefined`; `CYDPOD_ARENA_FAST=1` skips the sanitizer build; exit 77 = no media folder):

- (a) allocator unit tests: one acquisition, first fit, alignment, split/coalesce, sticky flag, reserve bump-only and disjoint from main, foreign frees ignored, null only beyond the reserve.
- (b) fixture through the arena: 131 frames, 134,144 samples per channel, ADTS (73,337 B) and PCM hash identical to the heap path; a second track on the same reader is identical with no second acquisition and nothing leaked at reset; a 1-frame track followed by the fixture (CY-5); destruction releases the arena; resident-table limit.
- (c) faked acquisition failure: `begin()` = `no_memory`, `write()`/`finish()` keep returning it, one acquisition, nothing held, hook never installed; the same reader plays once the memory returns; no leak.
- (d) forced exhaustion: main region swept from 16 B to 24 KiB for the fixture and a 200-frame ascending stream (672 runs): each run is either `ok` without the flag or `no_memory` with it; none overran the reserve or crashed; the block chain stayed consistent (`M4aArena::consistent()` catches a write across a block boundary, ASan a write outside the single block); nothing leaked; an exhausted reader plays the next track with a normal arena; the status is sticky.
- (e) hook hygiene: the slot is empty between calls; a foreign hook is neither used nor lost; two readers begun together and played in turn see only their own allocations (the vendor's sample-size cache is a function static shared by all instances, so two readers can never be fed at the same time; the firmware has one).
- (f) peak live bytes and per-call maxima (table above); 25 of 25 real files identical to the heap path.
- `m4a_caps_test` and `m4a_reject_test` also run unchanged in arena mode (`-DCYDPOD_ARENA_RUN`), peak = arena live bytes held to the bound; only the resident-table-at-16,384-entries case is skipped there (arena mode caps the resident table at 256).
- `build_m4a_hook.sh` repeats the CY-10b1 comparison through the arena: 56 heap allocations in the baseline run, 56 arena allocations, 0 bypassing, live 0.

### Results

All host suites pass: `build_m4a_{host,caps,reject,sequence,r1r5,filetable,real,hook,arena}.sh` (25 of 25 real files; the arena test also under ASan/UBSan with no finding).

`powershell -File tools/check.ps1` exit 0 (`.tools/logs/cy10b2-check.txt`). The probe still builds with the default `new (std::nothrow)` arena memory:

| | image bytes | static RAM | sha256 |
|---|---|---|---|
| CY-10b1 probe | 1,225,152 | 61,560 | `ed9411e5db76ed99ceca9177b3fb4a2097df573e865ca94e95da0a3999d62746` |
| CY-10b2 probe | 1,226,528 (+1,376) | 61,560 (+0) | `bd005e2fef9978b18e3bca2965d4c464e3fe6d4d7d054f35ee3e0771ff39ba2e` |

Compile only; no hardware was touched and nothing was flashed. Note for whoever flashes it: the probe's reader now takes one 28,672 B block from the heap in `begin()` instead of about 8.7 KB of scattered vendor allocations.


## CY-10b3: failure sweep, arena right-sizing, firmware wiring

Compile and host tests only; no hardware, serial port, SD card or flashing. Branch `cydpod/cy-10`.

### Result in one paragraph

The reader arena is now **16,384 B in total (main 11,776 + reserve 4,608), down from 28,672 B**, and the allocation-failure sweep passes for every allocation event on three inputs (plain and ASan/UBSan). The probe takes the arena with `heap_caps_malloc(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` and derives its admission from `cydpod::kArenaAcquireBytes`. **That is still not small enough to admit an `.m4a` at the measured board state:** after the file is open the board has 45,488 B free and the model needs 47,840 B, so the guard rejects with `low_memory`, **2,352 B short** (628 B short on the largest block; 304 B short even with a zero margin). The margin was not touched. Options for closing the gap are at the end; the decision is the coordinator's.

### Final sizes (`lib/cydpod_m4a/m4a_arena.h`)

| constant | bytes | meaning |
|---|---|---|
| `kArenaMainBytes` | 11,776 | live bound 11,440 + 336 fragmentation allowance |
| `kArenaReserveBytes` | 4,608 | bump-only fallback, plain request sizes |
| `kArenaTotalBytes` | **16,384** | main + reserve (`static_assert`) |
| `kArenaAlignSlack` | 8 | `heap_caps_malloc` only promises 4-byte alignment, so `open()` asks for 8 more and aligns the base itself |
| `kArenaAcquireBytes` | 16,392 | the one request made to the heap |
| `kArenaClassMinRequest` / `kArenaClassPayload` | 512 / 2,056 | requests in (512, 2,056] are served as one block size |
| `kM4aVendorSliceBytes` | 64 (was 256) | largest mdat payload slice handed to the vendor in one call |
| `kM4aArenaResidentEntries` | 64 (was 256) | resident (no-source) sample-size table inside an arena; the shipping mode is file-backed and has none |
| `kM4aArenaLiveBoundBytes` | 11,440 (was 11,520) | proven bound, derived below, computed from the constants |

### Allocator: why the arena can be this small

CY-10b2 used first fit and sized the main region 20,480 B for an 11,520 B bound because fragmentation reached 1.47x the live peak (16,128 B on the adversarial streams). The vendor's allocations are a handful of 1-2 KiB buffers (parser buffer, demuxer buffer, frame buffer, ADTS scratch) that `Vector`/`SingleBuffer` grow by allocating the new block before freeing the old one, plus a few small vectors. Two changes remove nearly all the waste:

1. **One block size for the large buffers.** Every request in (512, 2,056] B is served as a 2,056 B payload block (2,064 B with its header). A freed large block is therefore always exactly reusable by the next large request; growth never leaves a hole that no later request fits.
2. **Two-ended placement.** Large blocks are cut from the high end of the best-fitting free block, small blocks (callbacks, containers, level stack, resident table, early ADTS-scratch steps) from the low end, so the two kinds do not carve each other's holes. The search is best fit with coalescing on free.

Measured with `minMain` (the smallest main region, in 8-byte steps, with which the track still plays without touching the reserve: the real requirement, fragmentation included), over the fixture, 8 adversarial patterns x 4 slice sizes, 300 random frame-size streams and 25 real files:

| allocator | worst synthetic minMain | real files minMain | peak live |
|---|---|---|---|
| CY-10b2 first fit (high water, same kinds of input) | 16,128 | <= 12,040 | 7,544 .. 10,984 |
| best fit | 15,400 | <= 11,272 | same |
| best fit + class size | 12,728 | <= 11,208 | 11,016 |
| **best fit + class size + two-ended** | **11,208** | **<= 11,208** | **11,016** (real files 10,992) |

Fragmentation is now 192 B over the peak instead of 4.3 KB. The price is that the live peak no longer varies with the track: a class block is 2,064 B whether the frame is 400 or 2,000 B, so every input peaks at 10,992..11,016 B (before: 7.5..8.8 KB typical). That does not matter: the arena is fixed and its size is set by the worst case, which fell from 20,480 + 8,192 to 11,776 + 4,608.

### Proven bound on live bytes (11,440 B)

Same structure as the b2 table, with class blocks: five class blocks (parser buffer, demuxer buffer, frame buffer, ADTS scratch, and the predecessor of whichever of the last two is growing; only one grows at a time) = 5 x 2,064 = 10,320; callbacks 6 x 56 = 336 + 8; containers 17 x 16 = 272 + 8; level stack 8 x 8 = 64 + 8; resident table 64 x 2 = 128 + 8; and, conservatively, one small predecessor while a small vector grows (280 + 8). Sum **11,440** (`kM4aArenaLiveBoundBytes` is computed from these terms). The phase-aware figure is lower, 11,152 (the small predecessor exists only while containers/callbacks are built, when the ADTS scratch does not exist yet); the measured peak is 11,016 because the resident table is absent in the shipping mode. The main region adds 336 B for fragmentation (observed 192 B). The bound follows from the caps (`m4a_caps.h`), not from the file.

### Reserve: derivation and mechanical check

The reserve serves a vendor call that outgrows the main region, until the reader notices (`vendorDone()`), so it must cover the bytes **one** vendor call can request, in plain sizes (the reserve has no headers and is never reused):

| call | worst request volume |
|---|---|
| `demux.begin()` (parser + demuxer buffers) | 4,096 (4,112 with headers, the measured maximum over every test) |
| construction | frame buffer 1,024 + callback vector growth 1,344 = 2,368 (the worst exhausted run used exactly 2,368) |
| `write()` of the moov header | containers 2,448 + level stack 288 = 2,736 |
| one 64-byte mdat slice | frame buffer jump 2,048 + ADTS scratch jump 2,056 + strictly growing small steps inside the slice (< 250) + 8-byte rounding < 4,320 |

The slice was reduced from 256 to 64 B for this (a 256 B slice allows ~600 B of growth steps per buffer; the firmware feeds 256 B chunks, so each now costs four vendor calls instead of one, which is negligible). 4,608 B leaves 288 B over the slice bound and 496 B over the measured maximum.

Mechanical checks (all run by `build_m4a_arena.sh`):

- `m4a_arena_test` (f): every measured run requires `maxCallBytes() <= kArenaReserveBytes`; maximum over the fixture, adversarial, random and 25 real files: **4,112 B** (21..25 requests).
- `m4a_arena_test` (g): a stream with the growing-chain / jump-to-2,041 pattern placed at **every one of the 64 slice alignments** (128 runs, two chunk sizes): worst call 4,112 B <= 4,608 B, never exhausted.
- `m4a_caps_test` and `m4a_reject_test` in arena mode (105 malformed runs, 3 slice sizes each) now hold the per-call byte total to `kArenaReserveBytes` (they held the largest single request to the live bound before).
- Fallback disjointness by address ranges: `m4a_arena_test` (g) drives the allocator with 20,000 random alloc/free operations (small, class-size, oversize) against main regions of 1,024 B, 4,096 B and the real size with a 6 KiB reserve, resetting every 500 steps. Every grant is checked: main blocks never overlap each other, reserve blocks never overlap each other or any main block, everything stays inside the one block, 8-byte aligned; refusals (null) only when the plain request does not fit the reserve; the block chain stays consistent. 0 violations. The failure sweep repeats the same audit inside real vendor flows (below).

### Bound proof re-run and exhaustion fallback

- `build_m4a_arena.sh` (plain and ASan/UBSan): fixture, 25 real files, adversarial and random streams, chunk sizes 1 B .. 4 MiB: peak live <= 11,016 <= bound 11,440, `minMain` <= 11,208 <= main 11,776, never exhausted, ADTS and PCM identical to the heap path.
- Forced exhaustion (d): main region swept from 16 B to 24 KiB for the fixture, a 200-frame ascending stream and a chain-then-jump stream with 17 tiny frames before it (3 inputs): each run is `ok` without the flag or `no_memory` with it; the block chain stays consistent; the reserve never overran (worst exhausted run: 2,368 of 4,608 B); an exhausted reader plays the next track.

### Failure sweep (`tests/m4a_alloc_fail_test.cpp`, `tests/build_m4a_alloc_fail.sh`)

A counting run records every allocation event the reader can make while it begins, plays and is destroyed: the arena acquisition (`M4aArenaMemory`), every vendored-allocator request (the arena hook, observed through the new `M4aArena::Probe` test aid), and anything that bypasses both (global `operator new` and the `malloc` family, wrapped with `-Wl,--wrap`). Then for every k in 1..N the k-th event fails, in two modes: only the k-th, and the k-th and every later one ("sticky"). A hook allocation "fails" by being refused by the main region, so it is served from the reserve (the design never returns null from the hook) and raises the exhaustion flag; a failed acquisition returns null from the memory source (which, like `heap_caps_malloc`, hands out only 4-byte aligned blocks); a bypassing allocation would return null or throw. The harness itself never allocates inside the window (fixed-capacity containers, a one-pointer callback), so every event belongs to the reader.

Per faulted run it asserts: no abort, crash, hang or escaped exception (signal handlers and a timeout report the input and k); status `no_memory` for an accepted input (the rejection status or `no_memory` for a rejected one) and sticky on further `write()`/`finish()`; after `end()` arena live bytes 0, the memory source holds nothing, no hook grant is left, no bypassing allocation is left; every grant audited by address range (no overlap, inside the block); then the next valid track on the **same reader with its arena kept**, on the same reader **after `end()`** (arena acquired again) and on a **fresh reader** decode identically (ADTS bytes, frame count and hash; PCM decoded through Helix and hashed for the fixture, and for k = 1, N/2, N elsewhere).

| input | chunks | N events | faulted runs (single + sticky) | outcome |
|---|---|---|---|---|
| fixture `stereo-faststart.m4a` | 256 B | 45 (1 acquisition, 44 hook, 0 bypassing) | 90 | 90 `no_memory` |
| fixture | 4,096 B | 45 | 90 | 90 `no_memory` |
| fixture as a **second track** on a warm reader (arena kept) | 256 B | 44 (0 acquisition, 44 hook) | 88 | 88 `no_memory` |
| rejected `stereo-moov-last.m4a` | 256 B | 13 (1, 12, 0) | 26 | 26 `no_memory` |
| rejected `stereo-moov-last.m4a` | 4,096 B | 13 | 26 | 26 `no_memory` |
| real file `07-01 - Deee-Lite - Groove Is in the Heart.m4a` (read-only, 9,985 frames) | 256 B | 49 (1, 48, 0) | 98 | 98 `no_memory` |

418 faulted runs, 0 failures, plain and under `-fsanitize=address,undefined` (`.tools/logs/cy10b3-sweep.txt`). The rejected file's rejection (`moov_after_mdat`, raised at the mdat header) comes after all 13 of its allocations, so every fault there pre-empts it. Also checked: an acquisition refused in the `begin()` of the **second** track on a reader (track 1 ok, `end()`, track 2 `no_memory`, `write()`/`finish()` keep returning it, track 3 plays identically). Zero bypassing allocations is asserted: nothing the reader does touches the heap outside the arena hook and its one acquisition. The sweep found nothing to fix in the reader. A deliberate negative control (a refused request returning null instead of falling back to the reserve) hung the vendor's `stop()` path, which the sweep's timeout reports; that is the reason for the reserve design.

### Where the allocator hook is installed and cleared

`setAllocatorHook` has exactly one call site, `M4aGuardedReader::Scope` (`lib/cydpod_m4a/m4a_guarded_reader.h`): the constructor saves the current slot value and installs this reader's hook, the destructor restores the saved value (null in the firmware). `Scope` is instantiated in four places only: `begin()` (twice: construction plus primer, and the `demux.begin()` call), `destroyVendor()` (`~M4AAudioDemuxer`, `~M4aCappedSizes`, from `begin()`, `end()` and the destructor), and `vendorWrite()` (each `demux.write`). `finish()` and the guard's own scan call nothing in the vendor. In the firmware those entry points are reached from `M4aFeed::begin` (producer task, after `SD.open`), `M4aFeed::read` (producer task, inside the `decoder.next` lambda) and `delete feed` (producer task). Nothing else uses the vendored `Allocator` while the slot is set: the Bluetooth stack is ESP32-A2DP without `A2DP_I2S_AUDIOTOOLS` (it never includes AudioTools; nothing in the probe defines that macro), Helix allocates through its own `helix_malloc`/`libhelix::Allocator` (a separate class in another namespace), the sink and main tasks never call into the reader, and the frame callback and `SdSource::readAt` run inside the scope but allocate nothing through audio-tools containers (they `memcpy` and call `File::read`). The slot is a plain global written only by `Scope`; this remains a design rule that the code cannot enforce, which the tests check from the other side (slot empty between calls; a foreign hook is neither used nor lost; `m4a_hook_test`: 56 arena allocations = 56 heap allocations of the hook-less run, 0 bypassing).

### Firmware wiring (`firmware/cydpod_probe/cydpod_probe.ino`)

- `kArenaMemory` = `heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)` / `heap_caps_free`; `M4aFeed::begin` calls `reader.setArenaMemory(&kArenaMemory)` before `reader.begin`. If the block cannot be taken, `begin()` returns `no_memory`; the producer now checks `feed->failed()` right after `begin`, logs `TRACK ev=skip` and `ERR m4a no_memory`, deletes the feed and continues with the next command, before the decoder is allocated.
- `constexpr uint32_t kReaderPeakBytes = cydpod::kArenaAcquireBytes + sizeof(cydpod::M4aGuardedReader)` (16,392 + 4,344 = 20,736; was the hand-typed 14,336). `m4aNeedFree()` keeps its formula; `m4aNeedLargest()` is now `sizeof(M4aFeed) + kArenaAcquireBytes + kDecoderLargestRequest`, because the feed, the arena (one contiguous block) and Helix's largest request are carved in that order, all from the largest block in the conservative case.
- Compiler-reported xtensa sizes: `sizeof(M4aFeed)` 8,720, `sizeof(M4aGuardedReader)` 4,344, feed extra 4,376 (the 64-bit host layout is 9,392 / 5,000 / 4,392). `static_assert(sizeof(M4aFeed) <= 8720)` now guards the arithmetic below.

### Admission at the measured board state: does not fit

CY-12a: 45,488 B free and 45,044 B largest block after the file is open.

```
reader arena                     = 16,384 + 8 alignment slack = 16,392
kReaderPeakBytes                 = 16,392 + 4,344             = 20,736
need_free    = 20,680 + 20,736 + 4,376 + 2,048                = 47,840
free         = 45,488                                         -> 2,352 B SHORT   (rejected, low_memory)
need_largest = 8,720 + 16,392 + 20,560                        = 45,672
largest      = 45,044                                         ->   628 B SHORT
zero margin  : 20,680 + 8,720 + 16,392 = 45,792 vs 45,488     ->   304 B SHORT
```

The margin is unchanged at 2,048 B; nothing was tuned. Before CY-10b3 the same state "admitted" with 4,032 B of slack, but only because the model assumed a 14,336 B reader and a failed allocation inside it was not survivable. With the arena the demand is real, fixed and failure-safe, and it is larger. The floor for the arena is the live bound (11,440 B) plus the 4,112 B the reserve must cover for `demux.begin()`, about 15.5 KB, so no allocator work can reach the 14,040 B the state would allow (45,488 - 2,048 - 20,680 - 8,720).

Ways to close the gap, none applied (each needs a decision or a measurement the host cannot give):

1. **Ring 2,048 -> 1,024 frames** (`PcmRing`, a `.bss` object): not a usable option: the producer pushes whole 1,024-frame blocks and needs capacity for at least two blocks. Retained at 2,048 frames.
2. `AdtsDecoder::pcm_` 4,096 -> 2,048 samples: +4,096 B if the LC bound is proven (`cy11b-admission.md`).
3. Producer stack 8,192 -> 6,144 (+2,048 B): short by 304 B alone; together with sink stack 3,072 -> 2,048 (+1,024 B) it closes the gap.
4. A frame cap below 2,041 B (an AAC-LC stereo frame cannot exceed 1,536 B) would let the frame buffer and ADTS scratch use a smaller class; it saves roughly 1.5 KiB of main and 0.5 KiB of reserve and is a caps/policy change; not enough alone.
5. `M4aFeed::adts[4096]` is the ADTS queue between the reader and the decoder; with frames <= 2,048 B it could shrink somewhat; unmeasured.

### Test expectations that changed with the new sizes

- `m4a_arena_test` (a)/(b): the acquisition is `kArenaAcquireBytes` (main + reserve + 8); the 131-entry fixture through the **resident** table now returns `table_overflow` (the arena limit is 64 entries) and the identity check uses a 64-entry stream instead; `highWater` (an extent that two-ended placement makes meaningless) is replaced by `minMain`; the unit expectations (first-fit-like hole reuse, coalescing) still hold because the unit arena is smaller than a class block, where class rounding is off.
- `m4a_arena_test` (d): a third forced-exhaustion input; (g) is new.
- `m4a_caps_test` / `m4a_reject_test` in arena mode: the per-call request total is held to `kArenaReserveBytes`; the reject suite runs file-backed (the shipping mode, via `MemSource`) because its fixtures use 131-entry tables.
- `m4a_hook_test`: static arena block sized `kArenaAcquireBytes`; counts unchanged (56/56, peak live 10,992).

### Results

All host suites pass: `tests/build_m4a_{host,caps,reject,sequence,r1r5,filetable,real,hook,arena}.sh` and `build_m4a_alloc_fail.sh` (real: accepted=25 mismatches=0; the arena and sweep tests also under ASan/UBSan with no finding). `powershell -File tools/check.ps1` exit 0 (`.tools/logs/cy10b3-check.txt`); session dry-runs (`tools/cy11c_session.ps1`, `tools/cyd_probe_session.ps1`, `-DryRun`) pass in PS 5.1 and `pwsh`.

| | image bytes | static RAM | sha256 |
|---|---|---|---|
| CY-10b2 probe | 1,226,528 | 61,560 | `bd005e2fef9978b18e3bca2965d4c464e3fe6d4d7d054f35ee3e0771ff39ba2e` |
| CY-10b3 probe | 1,226,960 (+432) | 61,560 (+0) | `8da289bc871781f3ac4bebee37fea63a0dbc175d0229b87172e942b880690071` |

62.4% of the 1,966,080 B slot. Compile only; nothing was flashed. **Do not flash this image expecting an `.m4a` to play:** the guard will answer `ERR m4a low_memory` at the CY-12a heap state until the 2,352 B are found.

### CY-17 update: decoder PCM saving (compile/host only)

The proven LC-only output bound now permits 2,048 shorts instead of 4,096, returning 4,096 B of global `.bss` to boot heap; see [proof and validation](cy17-pcm-buffer.md). Static RAM is 57,464 B (was 61,560). No admission constants changed. Predicted post-open free: 45,488 + 4,096 = **49,584 B** versus need_free **47,840 B**, **1,744 B spare** beyond the unchanged 2,048 B margin.

The saving enlarges the first free region at boot, but the largest block after Bluetooth/file open is **unmeasured**. Best case: 45,044 + 4,096 = **49,140 B**, **3,468 B spare** against need_largest **45,672 B**. Conservative worst case: still **45,044 B**, **628 B short**, still rejected. This supersedes the historical free-heap shortfall above; it does not establish hardware admission or playback.
