# Classic list flick comparison

The W328C Bluetooth candidate is `compact-4096-flick` at `1cc1b4d`. The reference is the daily `compact-4096` image from `a532b5b`. Build the candidate with `tools/check.ps1 -Board jc2432w328c -SkipProbe -SkipHostMedia -LargePcmRing -BuildLabel flick`.

Classic motion paints use title bands and the newest offset at intervals of at least 33 ms. Four additional row slots use 960 bytes. The Bluetooth candidate uses 86,688 bytes of static RAM, 1,296 bytes above the daily image. The W328C confirms claimed list lifts on two valid zero-contact reports at least 8 ms apart. The next contact can catch the coast immediately. The comparison retains the momentum curve.

## Verification

- PASS CST820 report/decode, shared touch classifier and scroll-core checks. See `.tools/logs/flick-host.log`.
- PASS Classic UI, including both orientations, cache invalidation, motion paints, both ends and safe coast catch. See `.tools/logs/flick-ui-final.log`.
- PASS W328C Bluetooth and wired compile-only checks with the 4,096-frame PCM ring. See `.tools/logs/flick-w328c-final.log`.

Board gate pending. Use the same card and JBL Flip 3 in both orientations, paused and playing “90-Mile Water Wall.” Across ordinary motion, require at least 95% of poll and moving-frame intervals at or below 50 ms, and at least 95% titled fully visible rows up to 20 rows/s. Require `coastPaintMs` from 0 to 60 on at least 9 of 10 clean flicks per condition. Check `missingFrames` before and after steady playback while scrolling, capture heap minimum and largest free block, and confirm screen-off paint suppression. The owner selected the candidate as the 0.5 baseline after reporting clean overnight shuffle playback on 7 November 2026. The quantitative scrolling comparison remains pending.
