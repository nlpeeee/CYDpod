# CY-10b: internal allocation failure in the vendored reader (review R2)

Status: **part 1 of 3 done (CY-10b1: constructor-time allocator hook overlay)**. R2 itself (an internal allocation failure returns a status instead of aborting) is **not** fixed yet; b2 and b3 below do that.

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

## Still to do

**CY-10b2: fixed arena, deferred construction, sticky failure**
- A fixed arena (sized from the heap budget, `cy11-heap-budget.md`) and a hook whose `allocate` serves from it and, on exhaustion, records a sticky failure and returns a block that is safe for the overlapping-allocation case (for example a per-call scratch block large enough for the largest request, or a larger refusal that makes `Allocator::allocate()` not call `stop()` -- the overlay will need a second small change for that, since a null result currently reaches `stop()`/placement-new).
- Construct `M4aGuardedReader` (and its demuxer) in place inside the guarded wrapper on `begin()` with the hook installed, destroy and reset the arena between tracks, and remove the hook afterwards; check the sticky flag after every vendor call and map it to a new `M4aStatus` (for example `outOfMemory`).
- Interaction with `m4a_state_reset.h` priming and the file-backed table's window buffer.

**CY-10b3: sweep and evidence**
- Exhaustive failure sweep: for each allocation index N in a valid run (and in the rejection suite), make allocation N fail and require a clean `outOfMemory` status, no abort, no leak, and a fully usable reader on the next `begin()`.
- Regression suites for the new status and arena, add `build_m4a_hook.sh` to the standard suite list, and record the firmware size/RAM against the 1,225,152 B / 61,560 B figures above.
