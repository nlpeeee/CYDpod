# CY-43 heap implementation

Direct user request: save current state, implement the recommended investigation package, prepare to flash; do not flash or open serial.

Baseline: CY-42 3a4a617, rollback source bundle, dirty documentation patch and v4 images under `.tools/backup/pre-cy43/` in this isolated worktree. The hq research report is committed at 0ce2185. No main/sibling checkout edits, vendor edits or pushes.

Implement 512-byte audio/UI-file buffers with resize-failure diagnostics; retain 2048-frame PCM ring and MiniAAC workspace; reduce decode stack to 4096 based on measured FREE stack margins; create speaker task and PCM/mono buffers only on speaker route, park/stop before deleting/freeing on BT and handle allocation failure; allocate animation read/line buffers only while active; release source preview on leaving its context and rebuild on Back; release unrelated CJK strips; complete initial track open/queue restore before BT or defer BT until completion.

Keep loop/BT stacks, SSP and AVRCP, queue transaction guarantees and label capacities. Validate meaningful route lifecycle/failure and source Back behavior in host tests, run `powershell -NoProfile -File tools/check.ps1`, inspect image/ELF sizes, retain immutable flash artifacts and manifest with rollback/hash/flash command. No hardware performance claim until the later flash session.
