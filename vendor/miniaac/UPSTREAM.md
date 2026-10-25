# Vendored AAC core

- Repository: https://github.com/pschatzmann/codec-helix
- Commit: `f6e55a307c7577c5e3dfe1bf424a61075235fa08`
- Original location: `src/libhelix-aac`; vendored as `src/helix`.
- Selected utility headers came from upstream `src/utils`.
- Upstream `License.txt` is retained as `licenses/codec-helix-GPL-3.0.txt`.
  Individual AAC headers also retain the original RealNetworks notices.

Local changes, recorded in `patches/helix-minimal.patch`:

1. Replace the broad wrapper configuration with a minimal `ConfigHelix.h`:
   SBR and generated float trig tables are disabled; MP3/SBR sources omitted.
   Private GCC pragmas preserve wrapping arithmetic and disable strict aliasing
   when compiled by Arduino, which has no per-library CFLAGS hook.
2. Disable the heap allocation/free entry points in `buffers.c`. Only the
   preallocated arena path is used by the public component.
3. Let 64-bit MSVC use the existing portable arithmetic helpers for host
   validation instead of legacy 32-bit x86 inline assembly.
4. Detect SBR fill elements even without SBR enabled; reject these in the
   decoder instead of silently emitting the LC core.
5. Retain one program-configuration element instead of 16, saving 1,224 arena
   bytes. Raw blocks only use element zero; ADIF is not exposed. The legacy
   ADIF entry point is also bounded before parsing its PCE count.

No floating-point decoding or PSRAM allocator wrapper is included.
