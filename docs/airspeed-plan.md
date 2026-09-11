# Airspeed firmware implementation

Approved reference: `preview/airspeed.html`, including the latest 0–200 km/h scale, labels every 20, ticks every 5, 12-pixel edge bands, and shaded pointer. The user approved development and selected coexistence with the attitude indicator, with airspeed shown at startup.

## Approach

Use a separate portable `AirspeedRenderer`. Attitude retains the existing 466-pixel circular band/DMA pipeline; airspeed composes one rectangular update into exclusive PSRAM before TE-synchronized presentation. Generate the stationary face once in PSRAM. Rasterize vector font outlines and geometric shapes in code; cache the moving hand and its shadow for bilinear rotation. Keep per-frame work and allocations bounded.

The airspeed input is explicitly simulated until a real sensor is selected. Short BOOT switches instruments; long BOOT switches automatic demonstration. Serial `a` selects airspeed, `h` selects attitude, `c` calibrates attitude, `d` toggles demonstration, and `v135` followed by a newline sets a simulated speed. Simulation must remain identified on screen even while a manual value is held. IMU validity gates only the attitude instrument.

## Tasks

- [x] Add portable renderer contracts and failing behavioral tests for clamping, compact-band/full-frame equivalence, wire byte order, invalid ranges, and memory guards.
- [x] Implement the cached face, font outlines, antialiased hand/shadow, and native BMP export; compare the actual C++ render with the approved HTML at several speeds.
- [x] Add bounded simulated-speed control/parser tests and implementation, and route the selected instrument through the existing rendering/display stages without weakening attitude fault handling.
- [x] Run native regression/sanitizer checks, compile the ESP32 firmware, update usage documentation, and review the final changes. If the connected board is available, upload and inspect diagnostics using the session's existing flash authorization.

## Acceptance

The board boots to a full-circle airspeed dial matching the approved design. All speeds stay within 0–200; both instruments can be selected without stale bands. No per-frame allocation. FPS remains visible at bottom center; the simulated speed is never presented as IMU-measured airspeed. Firmware builds and native boundary tests pass; any hardware performance claims require measured serial evidence.

## Review and performance adjustment

The first hardware build ran at 21–23 FPS with approximately 34 ms in foreground drawing. RGBA texture sampling from PSRAM was replaced by two 8-bit opacity masks (24,384 bytes total), preferentially allocated in internal SRAM, plus analytic cream/metal material shading. The same 4×4 edge coverage and bilinear mask interpolation are retained. Native rendering at 135 km/h dropped from 0.565 ms to 0.277 ms; board performance is recorded separately in the validation report.

The HWCDC default TX buffer is 256 bytes; longer attitude diagnostics were consistently dropped by the nonblocking sender. Startup now reserves 1024 bytes, retaining the existing zero-timeout and whole-message capacity check.

An independent code reviewer could not be started because the agent tool reported its thread limit. Inline review covered allocation cleanup, immutable cache ownership, invalid numeric inputs, timer wrap, per-mode demo state, generation-based stale-band rejection, unchanged IMU fault policy, and FPS/SIM overlay bounds.


## Tearing and FPS revision

The user reported visible needle tearing and required airspeed throughput at least equal to attitude. Full-frame airspeed at roughly 32 FPS was rejected. The revised implementation:

- Composes the union of previous and new pointer/shadow bounds from the immutable dial cache; FIFO patch bounds include the previous requested speed. A generation change forces a full redraw before more partial patches, so discarded queued jobs cannot leave an old hand behind. A mode change initializes the whole dial once.
- Uses fixed-point bilinear masks and a cached center cap; no per-pixel square roots or floating-point alpha division in the moving layer.
- Enables CO5300 TE mode 1 (command 0x35, parameter 0), reads the board's GPIO 13, and waits for a fresh vertical-blank pulse after composition. A 30 ms timeout prevents a missing signal from freezing input and exposes misses in diagnostics.
- Copies the patch through two alternating internal DMA chunks with explicit completion before memory/descriptor reuse. The face and patch remain in PSRAM; there are no per-frame allocations.
- Counts completed presentations, including synchronization wait, and updates the FPS badge only when its displayed value changes. SIM remains visible in the unchanged bottom region.

Regression checks reconstruct full images from partial updates across smooth movements and large jumps, compare native/wire order, and validate copied DMA payloads and lifetime for arbitrary rectangles. Hardware measurements and visual confirmation remain the acceptance evidence for timing and tearing.


The first TE-synchronized candidate still drew and transmitted serially, taking two refresh periods (~30.4 FPS). Composition now uses the original three-slot ownership discipline across background/core 0, foreground/core 1, and DMA/core 0, with one exclusive PSRAM patch per slot. Transfer and preparation overlap. Each slot returns only after DMA completion or explicit stale-job rejection.

The user also observed dial jitter on the first partial-update candidate. Waveshare's BSP `rounder_event_cb` rounds BOTH starts down to even coordinates and BOTH inclusive ends up to odd coordinates. The candidate had only aligned X, leaving odd row origins. A regression assertion reproduced the missing Y alignment. Damage rectangles now align both axes. The odd-X FPS badge is presented within a restored 106×22 even-aligned border. `FastPanel::presentRect` asserts the hardware constraint.

Official source: https://github.com/waveshareteam/Waveshare-ESP32-components/blob/9daffb7168b3c093a330da700988a22092294eef/bsp/esp32_s3_touch_amoled_1_75/esp32_s3_touch_amoled_1_75.c#L393-L408

Final hardware revision reached 52.8-60.8 FPS during a 65-second automatic sweep, with zero TE timeouts and approximately 200 Hz IMU sampling. See `airspeed-validation.md`. The user confirmed that both dial jitter and pointer tearing disappeared after this flash.
