# CYDpod classic iPod-inspired touchscreen UI

Date: 2026-10-25. Revised: portrait and Artist/Album/Song browsing confirmed by the user.
Status: initial software implementation completed; hardware qualification pending.
See [implementation/evidence/limits](../research/classic-touch-ui-implementation.md).
Baseline inspected: `cydpod/miniaac`, commit `d9084b3`.

## Decision

**Keep the existing firmware and graphics framework. Redesign the presentation and navigation incrementally.** The desired appearance is feasible with the current TFT_eSPI primitives, screen callbacks, layout tables and calibrated XPT2046 input. Starting the firmware again would discard working integration without removing the ESP32's memory and timing limits.

Interpret “classic scrollwheel ipad” as a classic **iPod**. Use the colour-era iPod's menu hierarchy, blue selection bars, white lists, grey header, chevrons and compact Now Playing composition as the default reference. The device fills its display with the interface; it does not draw an iPod enclosure or virtual wheel. A monochrome-era variation could later share the same navigation.

This refines the visual direction of R5/C5/C6 in [the governing player specification](cydpod-plan.md). AAC-LC 44.1 kHz stereo, actual AirPods Pro 2 and external stereo line/headphone output remain required. This plan does not waive the G1/G2 audio gates or treat a compiled player as qualified hardware.

## Evidence from this checkout

The root [implementation checkpoint](../IMPLEMENTATION_STATUS.md) describes an older bootstrap state. Use the current source and newer, explicitly scoped reports when planning this UI work; do not mistake older statements for current implementation facts.

| Existing area | Observed implementation | Consequence |
| --- | --- | --- |
| Drawing | `display.*` uses TFT_eSPI; `ui.*` draws icons, rows, buttons and overlays from primitives | Existing renderer can draw the proposed UI |
| Appearance | `theme.*`, `style.*`, `layouts.*` separate palette, shape and player geometry | Add a coherent classic preset and adapt screen composition; a palette alone is insufficient |
| Screens | `ui.h` exposes enter/leave/tick/touch/press callbacks for Player, Library, Sources, Setup and Bluetooth | Add Home and a bounded navigation model; reuse screen lifecycle |
| Input | `touch.*` provides calibrated native 240x320 mapping, rotation, landing and live positions; `Screen::press` supplies down/move/up and claimed-drag suppression | Extend existing input routing with list dragging; do not replace the driver |
| Library | `screen_library.cpp` uses explicit page arrows; `tracklist.*` paints visible rows from an SD index; current playlists/folders/favourites already build queues | Reuse indexing and queue services; add browse state and scrolling |
| Audio | `audio.h` supplies asynchronous transport, volume, queue, shuffle, repeat, time and underrun telemetry | UI posts commands through this interface; it must not decode or read music in callbacks |
| Missing behaviour | M4A relative seek is explicitly a no-op. Current player report says filename titles, no MP4 tags/art and no position resume | Seek, Artists, Albums and artwork require functional work, not cosmetic wiring |
| Target memory | CY-13c reports `psram=0` on the identified board. CY-33 estimates only 20–27 KB free for its integrated player; this is unmeasured | Avoid framebuffer, expensive animations and large runtime caches; measure the integrated build |
| Qualification | [CY-33](../research/cy33-player.md) is compile-only; [CY-13c](../research/cy13c-final-image.md) is a different probe image's muted idle smoke | Neither establishes this player's audio/UI performance or required output acceptance |

The maintained application is `firmware/cydpod_player/app/`. Keep `vendor/` snapshots and their checksums intact. The compile check currently builds stock player, uploader, maintained CYDpod player and AAC probe; the stock player passing is not enough.

## Interaction and visual design

### Layout

Build for **240x320 portrait**, as confirmed by the user. Portrait is the product orientation and the default in the concept. Use the existing calibrated rotation mapping; no replacement touch driver is needed. Landscape is no longer a release requirement; retain existing support where it costs nothing, without spending implementation or qualification effort on a second layout.

- White list surface, dark text, pale grey header with a shallow gradient/bevel and blue highlight with white text.
- Single-column menus with right-hand chevrons. Highlight the pressed row immediately. Mark the playing track separately so “playing” and “under finger” are distinguishable.
- Use existing font infrastructure and cached Unicode strips. Clip/ellipsize long labels; optional title marquee only when audio has margin. No whole-screen scrolling text.
- Portrait starting geometry: 40 px header, six 40 px menu rows, 40 px footer. Validate these targets on the physical panel; they are proposed dimensions, not measured usability results.
- Back, transport and other direct actions use at least 40x40 px hit regions. Drawing geometry and hit regions come from shared layout data. The scrollbar is an indicator, not a tiny required control.
- Use real connection status. Omit battery percentage until actual battery measurement exists; the current project does not establish that capability.

### Navigation

```text
Home
  Music
    Artists -> artist's Albums -> album's Songs -> Now Playing
    Albums -> album's Songs -> Now Playing
    Songs [all titles, alphabetical] -> Now Playing
    Playlists -> Songs -> Now Playing
    Folders -> Songs -> Now Playing
    Favourites -> Songs -> Now Playing
  Now Playing
  Shuffle Songs
  Settings
    Audio Output -> Bluetooth / Line / Headphones [capability gated]
    Bluetooth -> existing discovery/known-device/retry/forget flows
    Playback -> shuffle / repeat
    Display -> brightness / calibration
    Library / Upload / About -> existing supported flows
```

Artists, Albums and Songs are required in the first usable UI, backed by the metadata index below. Playlists, Folders and Favourites remain additional routes. The Songs view browses song titles, independently of the filesystem's filename/path ordering. On a cold start show Home; playback restoration remains the engine's responsibility. Home's Now Playing row is disabled with an explanation when no track is available. Do not auto-play because the user entered a menu.

Use a fixed-depth stack (proposed maximum eight entries), storing screen, browse mode, parent/group ID, top row and focus. Back restores the exact previous browse location. A visible Home action gives a route out at the maximum depth. Do not keep dynamic screen trees or all track names in RAM.

**Browsing and playback are separate state.** Opening a folder, album or playlist must not call `playlist_select()` or replace the active queue: that existing function can start another song. Commit the selected group and chosen track together only when Play/track activation is requested. Define stable track IDs through the storage adapter and restore parent positions after Now Playing; index rebuilds invalidate stale browse entries safely.

### Touch behaviour

| Action | Touch adaptation |
| --- | --- |
| Choose/open | Tap a full row directly; one tap activates it |
| Scroll | Vertical drag; start with row-based scrolling without inertia. Add smooth/inertial motion only after device measurements justify it |
| Long lists | Large up/down page controls available as a reliable alternative, plus start/end access; measure 1,000-track navigation before considering an alphabet jump panel |
| Return | Persistent labelled Back target; do not require an edge gesture |
| Play/pause/skip | Three large previous, play/pause and next controls on Now Playing; preserve the existing previous/restart semantics |
| Volume | Visible volume button opens a slider and large minus/plus actions; debounce writes and save once after adjustment |
| Favourite/options | Explicit options action; existing long-press shortcut may remain optional |
| Seek | Initially read-only progress with honest unavailable feedback. Enable drag-to-seek only once the engine/container path supports and verifies it |

Implement a small gesture state machine: pending tap -> vertical drag after approximately 10 px movement -> release. Tune the threshold against measured panel jitter. Claim a drag immediately when it crosses the threshold so release cannot also select a row or favourite it. Cancel activation when the finger leaves its original button; a press should only commit on a valid release. This updates the existing landing-point activation behaviour deliberately.

Reuse release grace/debounce and `ui_pressClaimed()` protection. A long drag must not trigger the global four-second recalibration hold. Test this explicitly because calibration is intercepted in `app.ino` before ordinary screen events. Keep the existing recovery/calibration path available and make calibration reachable from Settings.

Only vertical list drag is needed initially. A horizontal back swipe, multitouch and circular scrolling add no required capability here. Do not require those gestures on the resistive panel.

### Now Playing

Provide track title, artist/album when known, queue position, elapsed/remaining time, read-only progress initially, previous/play-pause/next, volume, options and an output/status shortcut. Stack content vertically in portrait, with an optional 80x80 cover or small placeholder above the text. Avoid a persistent toolbar on every list that would shrink its useful rows.

Unknown duration displays `--:--`; unsupported seeking does not move a pretend playhead. Empty card, unreadable track, pairing, reconnect, disconnected output, missing playlist entries and index rebuild each have a clear recoverable screen/message. Reuse existing upload-mode transition and its persistence semantics.

## SD library catalogue

**Use a small, rebuildable catalogue on the SD card.** Extend the existing `/.sys/library.idx` approach with compact records and pre-sorted browse indexes; a SQL engine is not needed for these fixed browse operations. Audio files and their embedded tags remain the source of truth. Deleting or damaging the catalogue must only require a rescan, never loss of music.

Proposed logical layout under `/.sys/library/` (file names/encoding to be fixed at implementation):

| Data | Contents and use |
| --- | --- |
| Manifest | Schema/generation, counts, bounds/checksums and scan signature; validates the set of files before use |
| Track records + UTF-8 string storage | File path, title, track artist, album artist, album, disc/track number and available duration; numeric references avoid copying names into every browse index |
| Artist directory + membership | Artist display name and indexed artist -> album/track references; bounded page reads |
| Album directory + membership | Album identity/display name, album artist and ordered track IDs |
| Title order | Track IDs sorted by song title, with artist/album tie-breakers |
| Optional artwork cache | Separate later feature; browsing does not depend on artwork decoding or thumbnails |

Only the visible page, fixed navigation state and a small read buffer need to be loaded for browsing. Do not load every tag string or add a second whole-library object tree to RAM. Existing track/order allocations still count toward total memory. Build sorted views with bounded batches and disk merge passes if necessary, before starting Bluetooth/playback. A host-generated catalogue can be an optional acceleration; using a PC must not be required for ordinary SD-library use.

The first scan reads embedded **M4A metadata** through a suitable existing MP4/tag reader, not the retained MP3 ID3 parser. Current `storage.cpp` deliberately writes empty title/artist fields, and MiniAAC's present public adapter does not expose music tags; integrate a separately assessed existing metadata reader without changing the pinned decoder snapshot. Extract title, artist, album, album artist and disc/track numbers with bounded lengths/box traversal. Keep display metadata separate from sorting/grouping keys and preserve UTF-8 display text.

Initial grouping rules:

- Artists lists the track artist; selecting one lists albums containing that artist's tracks, then those tracks in disc/track order. Offer an All Songs row for the selected artist. Preserve a multi-artist tag as one display value initially rather than guessing that commas or ampersands split names.
- Albums groups by album title plus album artist, falling back to track artist. Use a compilation marker/Various Artists where available and a deterministic source-folder discriminator for otherwise ambiguous same-name releases. Never merge every album called “Greatest Hits”.
- Within an album sort by disc number, then track number, then title/path for missing or duplicate numbers. Songs sorts alphabetically by display title using documented deterministic sort keys; preserve accents/scripts in display text.
- Missing title falls back to filename; missing artist/album appears under Unknown Artist/Unknown Album. Do not infer factual artist/album tags from a folder name. Duplicate titles remain separate playable records.
- Selecting a song commits the visible group/order as the queue, starting at that song. Browsing alone leaves the active queue untouched. Track IDs are scoped to one validated catalogue generation; favourites retain their existing path identity across rebuilds.

On subsequent boots, reuse the validated catalogue when unchanged; scan for additions/removals/edits at startup or explicit Refresh Library. The present path+size CRC can miss a same-size retag, so extend the scan signature with usable modification times and provide a forced metadata rebuild. Timestamps/signatures are cache heuristics, not proof that content never changed. Update after uploads in the exclusive management mode. Do not rewrite the catalogue on every tap or scan music tags during scrolling.

Build a new generation beside the old one, check every record/reference/file length, and publish a small validated manifest only when complete. Keep the last valid generation recoverable across interrupted writes; do not assume FAT rename supplies transactional durability. Handle card-full/corrupt-index cases visibly and retain the playable previous generation where possible. No automatic media formatting or music-file edits.

## Implementation sequence

Effort ranges below are engineering estimates, not deadlines. Hardware access and audio blockers can extend them. Implement sequentially in the assigned checkout; this plan does not launch workers.

| Phase | Work and ownership boundary | Acceptance | Estimate |
| --- | --- | --- | --- |
| 0. Baseline and design | Record build/source identity, screen photographs and integrated playback heap/underruns. Exercise the HTML mockup and physical target sizes. Reconcile current evidence with the older checkpoint before implementation | Reproducible maintained-player build; design dimensions fixed; G1/G2 remain explicit prerequisites for hardware integration/qualification | 0.5–1 day, excluding audio gate work |
| 1. Visual primitives | Add classic preset in `theme.*`/`style.*`, portrait layout in `layouts.*`, shared header/row/transport primitives in `ui.*`. Disable GIF background for this preset through `background.*` integration | Coherent portrait Home/menu/Now Playing appearance, readable text and no clipped hit areas; current controls still work | 1–2 days |
| 1a. Required catalogue | Extend `storage.*` using an assessed existing M4A metadata reader; add versioned on-card records, artist/album memberships and title sort order with bounded build/read memory | Real tag-based Artist/Album/Song browsing, disc/track order, compilation/unknown/duplicate handling and recoverable refresh; no original file changes | 2–3 days |
| 2. Navigation and gestures | Add `screen_home.cpp`, bounded navigation and reusable list viewport; adapt `ui.*`, `screen_library.cpp`, `screen_sources.cpp`, `tracklist.*` and input cancellation. Reuse `touch.*` sampling; change driver only if measured jitter requires it | Home -> Music -> source -> track -> Now Playing -> Back restores position; browse does not alter queue; drag never activates a row or recalibrates | 1–2 days |
| 3. Player and settings integration | Recompose `screen_player.cpp`; add volume/options overlays; adapt Setup/Bluetooth navigation without losing retries/forget/calibration/upload. Add explicit capabilities for seek/output/artwork; preserve existing preference enum IDs and NVS keys | Transport/volume/shuffle/repeat/favourites operate during playback; unsupported features remain honest; existing settings/calibration survive | 1–2 days |
| 4. Optional artwork | Add bounded artwork extraction and prepared thumbnails to the existing catalogue; keep filenames/placeholders as fallback | Missing/large/malformed art does not affect Artist/Album/Song browsing or audio; 1,000-track library remains bounded | 1–2 days |
| 5. Seek/resume dependency | Audio-owner task: reuse sample/time/container tables and decoder reset support, add capability/error reporting and an asynchronous absolute seek command. Refill and resume safely; then connect progress interaction | Supported M4A seeks to within the governing proposed 1 s tolerance, retains stereo/rate, handles failures and restores position across reboot. Keep read-only progress until this passes | Separate estimate after reader assessment; not included in UI estimate |
| 6. Integrated qualification | Compile/host regression checks, portrait screenshots, gesture/navigation/catalogue tests and actual output playback with repeated navigation/artwork | UI gates below and applicable A2–A8/A10/A12 gates in governing spec pass; store evidence with exact image identity | 1–2 days plus required soak time |

A usable portrait classic UI with required Artist/Album/Song browsing is approximately **5.5–10 engineering days** (phases 0–3 including 1a), after prerequisite audio work. Optional artwork and integrated qualification add approximately 2–4 days. Seeking, unresolved AirPods/DAC work and any hardware redesign are separate; a complete iPod-style library is not a one-file reskin.

## Resource and integration rules

- Retain TFT_eSPI, current display/touch calibration, SD index, AAC engine, Bluetooth and uploader. Do not migrate to LVGL or replace firmware architecture for this design.
- A full RGB565 320x240 framebuffer is 153,600 B. An 80x80 cover is 12,800 B if held whole. Draw directly, update dirty regions, and stream thumbnails in short strips instead of retaining either buffer.
- Target at most 4 KiB additional persistent UI RAM versus the measured maintained-player baseline, with a separately counted scratch ceiling of 2 KiB. Reuse scratch where possible; these proposed caps do not override playback memory gates. Review actual static and heap deltas before acceptance.
- Avoid per-tick/per-track UI allocations. Keep navigation and visible row state fixed-size. Existing library/order allocations still need measurement at the proposed library size; do not describe current RAM use as independent of track count.
- Prepare metadata/indexes/title strips/thumbnail caches before playback or in the existing exclusive management mode. Version caches, write temporary then rename, and rebuild safely after interruption. Preserve original music files and existing supported Unicode behaviour.
- Never repaint the full screen in the steady tick. Bound list rendering work per loop, yield to servicing audio/Bluetooth, and reuse `audio_bufferLow()` to defer optional work. Do not animate GIFs in this preset. Cover changes and Unicode strip reads share SD bandwidth with audio, so throttle them too.
- Poll progress at a modest fixed rate (starting target 4 Hz), and only repaint changed fields. Press feedback should be immediate. Navigation transitions can be instantaneous; slide effects are optional after measurement.
- Keep active queues alive while audio references them; `audio_setQueue()` retains its pointer. Use one deliberate queue commit rather than reusing/overwriting its storage during browsing.
- Maintain one audio-engine editor during future implementation. UI integration consumes capabilities and commands; seek/output work stays a separately scoped dependency.
- Add new persisted enum values at the end or explicitly migrate them. A “classic preset” must not silently repurpose current `SKIN_CLASSIC` or old style indexes. Apply new defaults to new profiles and provide an explicit preset selection for existing devices.

## Verification and decision gates

Run `powershell -NoProfile -File tools/check.ps1` at each completed implementation milestone, retaining output under ignored `.tools/logs/`. Confirm the `cydpod-player` job passes and fits the locked 1,966,080 B app slot. Preserve pinned dependency inventories and notices.

Use meaningful host tests for the new pure logic: bounded stack push/pop, preserving list offsets, filtering/group-to-queue mapping, scroll clamping, tap/drag cancellation and unavailable capability handling. Do not add drawing tests that simply repeat pixel constants.

On the identified CYD, record:

1. Portrait photographs of Home, Artists, Albums, Songs, Now Playing, settings and error states; usable finger targets without a stylus.
2. Press feedback p95 below the governing 150 ms target during required AAC playback; define latency from detected press to visible highlight separately from release-time command activation.
3. Zero accidental track activations over 100 scripted/manual drags, including drags longer than four seconds, jitter near threshold, release outside target and screen changes during a press.
4. Repeated Home/Music/Artist/Album/Song/Back navigation, 1,000-track browsing, Unicode/long titles, duplicate titles and album names, compilations, multi-disc order, missing tags/art, stale caches, same-size retags, additions/removals, interrupted/full-card rebuilds and no-playback states. Measure cold indexing, warm startup and list navigation time.
5. Free/minimum internal heap at least the governing proposed 20 KiB floor, largest free block exceeding the largest remaining allocation by 4 KiB, and task stacks retaining 1 KiB or an explicitly measured alternative. Record allocator failures, SD stalls and underruns; do not substitute CY-33 estimates.
6. The required actual AirPods playback/track-transition test and one-hour playback per supported container on each final wired path with scrolling/artwork. Final qualification includes the original soak, reconnect and switching matrix; generic Bluetooth speakers and host tones do not satisfy it.
7. Persisted calibration, bonds, volume and settings, power interruption recovery and supported resume semantics. No automatic card formatting or unintended output/volume change during browsing.

If row dragging cannot meet audio/latency gates, keep direct taps and page controls while reducing redraws; this is a viable touch-first release. If optional artwork misses the memory gate, ship the classic menus and text player first. Reconsider a graphics-framework change only after a small measured prototype shows the present renderer cannot meet the required interaction; a firmware rewrite is not justified by appearance alone. After two failed corrections of the same blocker, preserve evidence and propose a fresh chat with a precise prompt as required by AGENTS.md.

## Reviewable concept and references

[Interactive concept](../research/classic-touch-ui-prototype.html): portrait-first Home/Music/Artist/Album/Song/Now Playing/settings flows. All music and output state are sample data. It is a design aid, not a firmware port, decoder, timing benchmark or proof of the resistive panel's behaviour. The retained landscape toggle is a comparison aid, not a second release requirement.

[Apple's iPod classic User Guide](https://cdsassets.apple.com/live/6GJYWVAV/user/ma1195_ipod_classic_160gb_user_guide.pdf), menu discussion pp. 8–9 and Now Playing pp. 24–25, supplies the visual/navigation reference. Touch-specific interactions and resource budgets above are this project's proposed adaptations. Existing code and the linked local reports supply feasibility evidence.

This planning task's check result and source caveats are recorded in [planning validation](../research/classic-touch-ui-planning-validation.md). Hardware acceptance remains pending.
