# Foundation correctness review

Reviewed 2026-10-23, dispatch `ctx_a2629ef9b464`. Scope: setup/check/bootstrap, dependency lock and provenance, the serial ADTS probe, host tests/media verification, and README. Implementation owners remained active; this review changed only this report and ran no installation, firmware compilation, device operation or Git mutation.

**No blocking correctness findings remain in the reviewed source snapshot.** Two P2 findings were routed to their owners and corrected during review. This is a source/host-harness review, not completion of the hardware gates or independent certification of the final compile.

## Findings, ranked by severity

### P2 — Missing fixtures could silently pass the host matrix — resolved

The original `tools/media_check.py` loop enumerated whichever fixtures happened to exist and printed PASS even for an empty directory. Removing a positive bitrate or a negative M4A case silently reduced coverage. An in-memory reproduction, with subprocesses and report writes stubbed, produced:

```text
PASS synthetic fixture matrix; report fixtures/verification.json
Report file count: 0
```

The correction requires all ten nonempty named inputs at `tools/media_check.py:14-23`, checks before build at `:75`, and iterates the explicit inventory at `:85`. Regression cases are in `tests/test_media_check.py:7-22`. Reviewer independently exercised the current inventory function using in-memory paths, without modifying fixtures or shared outputs:

```text
empty: correctly rejected
complete: accepted 10 fixtures
partial: correctly rejected
zero-byte: correctly rejected
```

### P2 — Setup invoked an unchecked cached CLI and could not repair it — resolved

Originally, `tools/bootstrap.py:75-79` extracted the hash-checked CLI archive only if the executable was absent. A corrupted/replaced executable survived repeated setup and was executed at `:114` before `tools/check.ps1:28` could detect its mismatch. The correction at `tools/bootstrap.py:77` compares the cached executable to `executableSha256` and reconstructs it from the pinned archive on mismatch.

Demonstrating test: seed a disposable tool directory with invalid executable bytes, invoke bootstrap with downloads/core installation mocked, and assert the locked executable digest at the first subprocess boundary. The build owner supplied `.tools/logs/cli-repair-check.log`; reviewer read:

```text
PASS: corrupt disposable CLI reconstructed from pinned archive before first subprocess invocation
```

The reviewer also independently confirmed the current real CLI digest matches the lock, without executing it.

## Correctness paths examined

- **Ring/output lifecycle:** `audio_core.h:12-39` publishes producer/consumer indices with release/acquire ordering. The producer owns resets and decoder/file lifetime. `cydpod_probe.ino:55-95` increments consumer counters before inspecting enable flags, disables consumption before waiting for readers, then clears the ring and deletes I2S. No confirmed concurrent reset or I2S use-after-free was found. This assumes the library's single A2DP callback consumer contract; host ring stress does not validate Bluetooth scheduling.
- **Callback and task behavior:** the A2DP callback uses a fixed 64-frame local block, copies into potentially unaligned caller storage, and performs no SD reads, decoding, logging or waits. I2S writes have a 20 ms timeout, failures mute, and task loops yield. Queue/task allocation failures leave serial controls disabled (`cydpod_probe.ino:225-231`). Producer and sink stacks, heap minima and sampled largest blocks are instrumented; target watchdog margins and Bluetooth library allocations remain unmeasured.
- **Media bounds:** `audio_core.h:44-54` validates ADTS sync/layer, profile/rate/channels, block count and minimum header length. `adts_decoder.h:24-40` bounds compressed frames to 2,048 bytes, handles partial reads/truncation, checks decoder return/remaining bytes and verifies the exact output contract. Its output storage covers the documented defensive stereo bound. Existing negative tests exercise unsupported rate/channel/profile, malformed/truncated input and both unsupported M4A layouts. CRC validation, exhaustive malformed-payload fuzzing and detecting every implicit SBR extension are not established by these tests.
- **Setup/check safety and provenance:** archive extraction checks resolved containment and rejects archive symlinks; isolated library replacement checks containment before recursive deletion (`bootstrap.py:41-60,120-128`). Compile staging cleanup uses fixed job paths and a containment guard (`check.ps1:49-56`). No serial/upload/erase/SD-format command was found in setup/check. Reviewer independently verified all 95 vendored source hashes, both committed index hashes and the CLI hash. Lock entries, retained notices and the explicit Helix patch preserve provenance rather than claiming one license covers all dependencies.
- **Honest capability claims:** README explicitly preserves AAC-LC 44,100 Hz stereo, actual AirPods Pro 2 and stereo I2S requirements (`README.md:3-5`). Pins 22/27/17, GPIO17 LED/loading/access concerns, external headphone amplification and the shared digital line/headphone path are qualified at `:38`. Source and reports do not claim actual hardware compatibility from synthetic tests.

## Remaining evidence and specification work

These are declared follow-up gates, not requests to implement the full UI before the hardware feasibility gate:

1. **Final compile evidence obtained:** the build owner's default `powershell -File tools/check.ps1` completed during review. Reviewer read `.tools/logs/check.log` ending with `PASS synthetic fixture matrix: 3 positive, 7 negative` and `PASS: compile-only checks completed; no device commands were run.` The updated `.tools/logs/build-results.json` records player **1,336,576 bytes**, uploader **1,147,600 bytes**, and AAC probe **1,189,936 bytes**, with probe SHA256 `35ec4065ba349757d39d9eee7f662249976ba281a9dc5e1765153f8ac7f233e4`. This supersedes the earlier stock-only checkpoint. The reviewer inspected this evidence without starting a competing compile; preserve the owner's final logs and build report.
2. **Hardware lifecycle/timing:** identify the actual CYD before port access. On the verified harness, exercise the specified repeated output switches, pairing/reconnect and one-hour AAC runs; capture watchdog/reset counts, steady-state underruns, task margins and minimum heap/largest blocks. `a2dp.end(false)` retains controller allocations, and upstream discovery shutdown still waits on events; bounded shutdown/restart and no progressive heap loss require target evidence. No confirmed source defect is inferred solely from those pending measurements.
3. **Actual media and M4A:** the probe intentionally supports ADTS only and rejects both M4A layouts. The existing-reader integration and memory/table bounds described in `docs/research/aac-prototype.md`, section “Exact M4A follow-up,” remain required follow-up work; neither rejection tests nor a candidate reader establishes M4A playback. Required original files must remain unmodified and be tested once supplied.
4. **Analog output:** line/headphone select independent levels on the same I2S signals; the physical DAC/amp, headphone load, safe gain, clocks/stereo ordering, DC/noise/clipping and switch/mute transients remain unverified. Digital mute does not remove samples already queued in DMA. Test dummy loads and measured levels before listening; no acoustic-safety or AirPods interoperability result is claimed here.

Only `docs/research/foundation-review.md` was written by this reviewer. The two resolved findings must not be carried forward as open defects unless subsequent edits regress their fixes.
