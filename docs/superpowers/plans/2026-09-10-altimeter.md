# Altimeter Implementation Plan

User requested direct implementation without another confirmation. Reference is the earlier cream-on-black three-pointer altimeter image; the user subsequently specified meters. Continue inline in the existing feature workspace.

Goal: add a third instrument without changing airspeed startup or the validated attitude installation axes.

Design: 466-pixel black dial, gray inner disc, cream 0-9 numerals and 50 ticks, ALT / 100 METERS lettering, three continuously moving pointers (1000 / 10000 / 100000 meters per revolution), metallic center, soft shadows and the striped lower detail. Simulated altitude ranges from 0 to 99999 m and remains marked SIM. BOOT cycles airspeed -> attitude -> altimeter -> airspeed. Long press retains each mode's demo state; serial t selects altitude and b2750 plus newline sets simulated meters.

Rendering: a separate portable AltimeterRenderer shares cold vector lettering/geometry helpers with airspeed. Antialiased convex scanlines fill opaque interiors cheaply; three shadow layers merge into a small internal alpha scanline before one PSRAM pixel pass. Conservative old/new pointer bounds produce up to 15 non-overlapping horizontal regions in scan order. Each of the three exclusive PSRAM slots has its own immutable prepared pose. Core 0 publishes the pose, both cores draw alternating regions, then a completion queue joins both writers before DMA can read the slot. Regions are presented after TE with even X/Y/width/height. Clipped FPS/SIM restoration handles even narrow partial intersections; SIM has an opaque backing over the circular ticks. There are no per-frame allocations. Existing airspeed background and foreground pixels remain equivalent.

- [x] Add failing tests for the three-mode cycle, independent demo/input state, numeric rejection, scale clamp, and altitude timer wrap; implement controls.
- [x] Add renderer tests for the three independent needle ratios, native/wire equivalence, partial-patch reconstruction across wraps, aligned windows and guards; implement shared drawing helpers and altimeter renderer. Export and inspect real C++ renders.
- [x] Route altitude through the existing background/foreground/DMA ownership pipeline, restore status badges after affected patches, and retain IMU validity checks only for attitude. Compile and run existing regressions.
- [x] Flash under existing session authorization, inspect all three modes, altitude range boundaries and continuous rendering, and document actual FPS and timing. Leave altitude demonstration visible without changing the startup preference.

Verification: tools/altimeter_test.sh, tools/airspeed_test.sh, tools/test.sh, tools/fast_panel_test.sh, tools/build.sh, and a bounded hardware serial capture. Visual QA uses actual rendered firmware images; no user approval gate was requested.

Completed: final firmware write verified. The 65-second altitude demonstration measured 46.7–60.7 FPS (median 50.6), fixed 2750 m measured 60.5–60.7 FPS, and the existing airspeed/attitude modes retained their expected rates. Heap stayed at 14,436 bytes and TE timeout count stayed zero. Full details and binary hash are in docs/altimeter-validation.md and docs/altimeter-board-measurements.json.
