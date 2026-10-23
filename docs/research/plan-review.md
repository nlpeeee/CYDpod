# CYDpod plan review

Reviewed 2026-10-23: `docs/specs/cydpod-plan.md` and supporting `reuse-options.md` / `cyd-hardware.md`. No critical or high-severity correctness findings.

## P2 — Require wired endurance tests after the complete UI is integrated

**Locations:** `docs/specs/cydpod-plan.md:137–142`, `:164–170`; supporting requirement at `docs/research/cyd-hardware.md:167`.

C4 requires one hour on each output, but runs before C5/C6 introduce the full UI, metadata, artwork and library indexing. The final acceptance matrix explicitly repeats prolonged playback with screen activity only for Bluetooth (A2); wired A4/A5 specify electrical/listening checks without a duration, UI workload or explicit zero-underrun/memory gate. C5's general “without audio regression” wording leaves the wired release gate ambiguous. The supporting hardware report explicitly proposes one hour per selected output/container with UI activity, but the plan declares supporting research non-normative at line 3.

**Action:** Make C9/A4/A5 explicitly repeat at least one hour of required AAC playback for line and headphone output, for each supported container, on the final integrated UI/library build. Require zero steady-state underruns/resets/allocation failures and the existing memory gate while scrolling, changing tracks and displaying bounded artwork. Keep electrical/load checks alongside these runs. This closes a release-test gap without changing the proposed architecture.

Hardware remains unverified: no owned-board identification, GPIO continuity, AAC timing/heap measurement, wired electrical test or actual AirPods Pro 2 interoperability test was performed in this review. No existing repository check command was found; review validation is document/reference inspection only.
