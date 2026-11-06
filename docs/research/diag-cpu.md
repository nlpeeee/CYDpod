# DIAG-CPU

Base: 96e95b9 (v35); implementation: 9a953b6, branch cydpod/diag-cpu.

Added fixed-name CPU sampling once/second from loop, with the existing 10 s
stat cadence reporting accumulated wall/runtime deltas. New/absent/recreated
tasks reset their baseline; cached handles are revalidated by name each sample
before use. Runtime counters are microsecond uint32_t (SDK esp_timer backend),
unsigned subtraction handles the 71.6-minute wrap; samples must be less than
one wrap apart. Percentages are integer tenths of one core, clamped to 100%.
Other is both cores' busy share minus the explicitly printed tasks/writer,
clamped to zero; UI/controller work remains in other.

Serial formats (order preserved):
```
cmd: cpustats ok tasks=<n found> timer=esp_timer
stat: cpu dtUs=<us> idle0=<pct> idle1=<pct> m4adec=<pct> loopTask=<pct> BTC_TASK=<pct> BTU_TASK=<pct> BtAppT=<pct> writer=<pct> other=<pct> unmapped=<n>
stat: tasks <name>=<core>/<prio>/<stackFreeBytes> ...
```
Tasks line: first existing stat slot at/after uptime 20 s, then every 60 s;
core `*` means unpinned. Stack high-water values are IDF bytes. Writer sums
spkout/wiredout deltas (normally mutually exclusive).

Verified SDK config enables trace, runtime stats, esp_timer/U32, task handle
lookup and stack high-water. Controller name `btController` verified in the
pinned SDK ld/libbtdm_app.a string table. No running task-list inspection was
performed: common task rules prohibit device operations. Controller discovery
and actual serial/timing acceptance remain owner hardware checks.

Ordinary sample: at most 9 vTaskGetInfo(pdFALSE,eInvalid), 9 bounded task-name
lookups and 2 idle-counter reads, one reusable stack TaskStatus_t, no stack
scanning, allocation, new task or SDK changes. This small bounded path targets
<300 us; no host timing is claimed as hardware evidence. Sparse task-detail
logging additionally scans high-water stacks and writes serial output.

Checks (implementation commit; retained logs under .tools/logs):
- WSL Debian: sh tests/build_cpu_stats.sh
  `PASS: cpu stats arithmetic (wrap, percent, clamp, task lifecycle)`
- powershell -NoProfile -File tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia
  `PASS: compile-only checks completed; no device commands were run.`
  Both player and wired images: `PASS single pool` (40960 B).
  Logs: diag-cpu-host.log, diag-cpu-check.log; image size logs retained.
- Baseline int7/.tools/logs/codec-pool-v35-jc2432w328c.log:
  `Global variables use 95176 bytes`; after: `Global variables use 95344 bytes` (+168 B linked).
  CPU state symbol: 184 B .bss; name table: 36 B read-only.
  Baseline sketch 1582501 B -> 1584021 B (+1520 B).
  Wired baseline int7 v33-wired log: 95184 -> 95352 B (+168 B).

Diff review: app.ino only adds include, setup baseline and one loop stat hook;
audio/UI/route behavior is retained; forbidden files untouched. Host test and
compile establish arithmetic/build behavior only, not FLAC/BT or AirPods success.
