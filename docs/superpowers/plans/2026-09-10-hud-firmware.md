# HUD Firmware Implementation Plan

**Goal:** Port the approved round HTML HUD to the ESP32 alongside the three existing instruments.
**Spec:** `preview/hud.html` and its latest user-approved changes, recorded in `design-qa.md`.
**Architecture:** Portable raster renderer, immutable per-slot drawing lists and scan-ordered damage bands. Reuse the existing three PSRAM slots and TE/DMA ownership; do not allocate a full extra display frame. Real IMU pitch/roll; simulated heading, airspeed and altitude are documented as such. No fabricated heading sensor.
**Tech:** C++11, ESP32 Arduino, CoreText-generated OFL monochrome glyph subset, host sanitizers.

## Constraints
- 466px round display, neon green, 0–200 km/h and 0–3000m, existing USB-left axis mapping.
- Preserve centered readouts, smooth scale emphasis, identical 12px pointer triangles, double center circle, mechanical digits, removed side triangles/stems and no footer HUD text.
- Keep airspeed boot default. BOOT cycles Airspeed → Attitude → Altimeter → HUD; `u` selects HUD. Long press/d toggles HUD's own demo; c restores real attitude calibration.
- Keep unrelated dirty files and existing instruments; no commit/push requested.

## Work
- [x] Font: generate regular/bold compact alpha atlas and include OFL notice; visually inspect representative glyphs.
- [x] Controls: fail a four-mode selection test, add independent HUD demo state and filtered altitude-rate accessor, rerun control regression including timer wrap and invalid records.
- [x] Renderer: create `HudInput`, `HudRenderer::begin/prepareFrame/renderPrepared/render/damage`. Test guards, native/wire equivalence, scan-band reconstruction across wrap/roll/counter transitions, invalid inputs and slot independence; render actual C++ frames for visual comparison.
- [x] Integrate: extend `DisplayJob`, pipeline begin/background/present paths and sketch. HUD lists prepared once, alternating regions rasterized on both cores, wait for both before TE presentation. Preserve generation cancellation and frame-slot ownership. HUD handles invalid IMU by withholding ladder and showing IMU status while simulated tapes remain available.
- [x] Verify: host sanitizer/regression suites, Arduino build, inspect native renders, flash connected board and measure logs if accessible. Record measured FPS, heap, sampling, TE misses; do not claim unseen device visual results.

## Progress / decisions
- Existing feature checkout contains all approved uncommitted prototype and instrument work; continue here to preserve that exact working state.
- Font worker owns generator/atlas/license; main implements renderer/pipeline; controls worker may own controls and focused tests.

- Host sanitizer, thread sanitizer, existing instrument regression and Arduino compile complete. Native visual review and independent 2,000-frame damage fuzz complete.
- Review fixes: invalidate generation after discarded HUD pose so next frame clears the whole screen; separate real ladder roll from bank limit, normalize finite extreme roll before trigonometry.
- Initial flash review block resolved by explicit user authorization “允许自动烧录”. Enlarged firmware flashed with verified hash; 96-second serial run recorded in docs/hud-board-measurements.json. HUD median 16.3–16.6 FPS; physical tearing not inferred from logs.
- Readability update: 26px primary values, 14px side numbers, 19px selected labels, 208px shared span, 17/9px ticks, larger center and triangles; seven damage bands. Software division hoisted out of pixel loops; no claim of measured FPS improvement versus initial smaller UI.

- Latest readability correction: sparse perimeter ticks and 18–22px labels; pitch alternates 10° long / 5° short marks at 90px spacing, major labels only. Whole pitch labels remain bright until hidden at the boundary. Verified and flashed; see docs/hud-sparse-validation.md.

- Final pitch refinement: 5m/20m altitude tape; 60px pitch row spacing with every row labelled, 160/96px alternating lines, negative dashed lines and 6px end hooks. Expanded horizontal label region and precomputed actual glyph bounds replace conservative rectangles. Row clipping reduces diagonal raster work; final measurements in docs/hud-hooked-validation.md.
