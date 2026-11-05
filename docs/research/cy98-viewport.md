# CY-98 viewport: needs split

Base: 8924c3b; branch: cydpod/scroll-viewport; assigned worktree created.
No firmware or test changes. CHUNKING.md requires an oversized chunk to return
`worker_done`, outcome `failed`, subject `needs split`, before implementation.

## Scope findings
- Page.top, Gesture.scrollTop and row hit tests use row steps; pixel state must
  also survive navigation/mini-player returns and search invalidation.
- cache[6], dirty bits, loadRow, paintRow and activation all assume aligned row
  slots. Partial offsets need seven visible slots in portrait, keyed by absolute
  row, clipped opaque painting, placeholder handling and no SD reads during drag.
- Existing row drawing constructs String labels/details on every paint; the
  literal no-heap-per-frame requirement needs an explicit allocation-safe text
  path, rather than merely adding Tracker and a viewport clip.
- Paging assumptions occur in miniPlayerRegression, tapSettingsRow, geometry
  goldens and navigation scenarios. Removing the widget requires changing these
  along with the new clamp/tap/drag/thumb tests and production PNG captures.
- Combined implementation plus test migration is estimated above the playbook's
  ~300 changed-line / ~20 agent-minute limit. No scroll_core.h bug established.

## Proposed follow-on chunks
1. C3a renderer/cache seam: pixel Page offset, keyed existing-sized cache plus one
   slot, clipped opaque rows, placeholders and allocation-safe paint path; update
   row-step tests and geometry. Focused host test plus both compile-only boards.
2. C3b input/thumb: Tracker with slop subtraction, latest-offset paint once per
   tick, hard clamp/no fling, no reads during drag; replace page strip and input
   with transient draggable thumb. Migrate paging tests, focused behavioural test,
   two or three production PNGs, both compile-only boards.

## Evidence / open work
Checks: NOT RUN (report-only; no firmware implementation or final firmware tip).
RAM/flash delta: NOT MEASURED; codec pool and audio paths untouched.
No export, device, serial, media or network operations. No push.
Owner feel-test after implementation: 1:1 claim/drag, partial-edge clipping,
release stop, thumb endpoints, placeholder recovery, audio stability and themes.
The coordinator must approve the smaller scopes; all requested implementation
and acceptance checks remain outstanding.
