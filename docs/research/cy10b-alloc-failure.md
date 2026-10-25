# CY-10b: internal allocation failure in the vendored reader (review R2)

Status: **part 2 of 3 done** (CY-10b1: constructor-time allocator hook overlay; CY-10b2: fixed arena, deferred construction, sticky failure). R2 is fixed for every path the guard admits; CY-10b3 (exhaustive sweep, firmware hook) remains, see the end of this file.

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

## Still to do (CY-10b3)

- Exhaustive failure sweep: make allocation N fail for every N in a valid run and in the rejection suite (the arena sweep above varies the region size, not the individual request) and require a clean status, no abort, no leak, and a usable reader on the next `begin()`. Include a fault on the arena acquisition in the `begin()` of a second track.
- Check the reserve bound mechanically: instrument the per-call request total for adversarial frame-size streams delivered as one 256-byte slice at every alignment, and for the container/level-stack single-shot calls.
- Wire the firmware hook (`heap_caps_malloc(..., MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)`) through `setArenaMemory`, size the firmware admission (`kReaderPeakBytes`, `m4aNeedFree`, the CY-11b margin) from the 28,672 B arena, and confirm on the device that no other task allocates through the vendored `Allocator` while a vendor call runs (the design rule above).
- Decide whether the 20 KiB main region can shrink (best-fit, or placing the long-lived blocks first, would cut the 1.47x fragmentation).
