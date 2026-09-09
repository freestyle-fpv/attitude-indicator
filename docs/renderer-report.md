# Renderer implementation and validation

The portable C++11 renderer in `firmware/AttitudeIndicator/renderer.cpp` generates the native 466×466 RGB565 image. It has no Arduino dependency except its ESP32-specific PSRAM allocator. `begin()` is idempotent, returns false if allocation fails, and precomputes sphere depth and dithered sky/ground shades. The lookup allocation is 1,302,936 bytes; the caller supplies the 434,312-byte frame buffer. Rendering performs no allocations and no per-pixel trigonometry. Drawing strokes uses local distance calculations for antialiasing.

The geometry preserves a complete circular lower hemisphere, dark recessed inner rim, curved pitch ladder, rotating white roll scale and fixed yellow pointer, and fixed aircraft with a lower support and pedestal. White pitch marks rotate with roll; positive pitch moves the horizon down, and positive roll moves the horizon counterclockwise. The inner sphere projection bends its horizon under nonzero pitch. The outer annulus horizon remains a straight diameter, rotates with roll, and is independent of pitch. A flag packed into the precomputed depth table distinguishes annulus pixels without per-frame square roots. Invalid nonfinite inputs or render-before-begin yield a black frame; application status handling is owned by the firmware.

## Evidence

- Confirmed the previous scaffold failed the existing renderer suite with 10 expected failures before implementing.
- Native C++11 compilation with `-Wall -Wextra -pedantic` succeeds.
- Renderer tests pass, including complete circular clipping, bottom fill, pitch/roll directions, inverted sky/ground, fixed yellow aircraft/pointer, white bank-scale motion, pitch-independent annulus, safe frame bounds and roll wrap continuity.
- AddressSanitizer and UndefinedBehaviorSanitizer test run passes with no diagnostics.
- Across +179.9° to −179.9°, the mean absolute difference summed over RGB565 channels is 0.3925 out of 125, below the 0.5 continuity threshold (0.4% of total channel range).
- Inspected generated level (0°, 0°), reference-pose (−17°, −2°), and right-bank (0°, 30°) images. Reduced crowded pole markings and applied ordered dithering to reduce RGB565 gradient bands.
- The native CLI `render_frame PITCH ROLL OUTPUT.bmp` emits a 466×466, 24-bit BMP converted directly from the shared RGB565 frame. Invalid numeric inputs return exit status 2.

Native build example:

```sh
c++ -std=c++11 -O2 -Ifirmware/AttitudeIndicator tools/render_frame.cpp firmware/AttitudeIndicator/renderer.cpp -o /tmp/render_frame
/tmp/render_frame -17 -2 /tmp/attitude-reference.bmp
```

The two review corrections were covered by new tests that initially failed three expected checks, then passed after implementing moving bank markings and a pitch-independent outer horizon.

Actual board color, rendering time, display throughput, and approximately 30 FPS target still require hardware measurement. The main firmware task owns ESP32 compilation and full integration validation.
