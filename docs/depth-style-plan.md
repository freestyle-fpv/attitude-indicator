# Depth styling — September 10

Requested: a recessed inner rim and pointer shadows resembling the supplied
mechanical instrument, removal of the bottom trapezoid, smoother circular edges.

Implementation: precompute a directional inward shadow, thin dark lip and
4×4 area sampling around the rim into the existing RGB565 palettes. Cache a
single translucent shadow operation per pointer-shadow pixel, drawn after the
moving marks. Extend the yellow stem behind the lower rim, using a sampled aperture to
occlude its end and a small contact shadow. Keep the full circle, USB-left
orientation and display pipeline. No per-frame allocation or image assets. An immutable 217,156-byte PSRAM
transmittance table attenuates the horizon at the shaded rim. This fixes bright
pixels discovered where the rotating white horizon crossed partially covered
rim pixels; regression checks reproduce both level and 15-degree crossings.

Validation: fail the new depth-style behavior checks against the previous
renderer, inspect level and tilted native previews, run existing raster/staged/
compact/wire and allocation-failure tests with the new shared paint definitions,
build, flash with readback verification, and measure the resulting frame rate and
sensor stability. Preserve the prior stable sources, firmware and previews in
`build/before-depth-style` for comparison and recovery.

Final refinement: move the 104×22 FPS badge to (181,432), horizontally centered
inside the lower circular perimeter and below the DEMO legend. Stable per-row
indexing adds 1,868 bytes in PSRAM and reduces repeated shadow/AA cache scans
from 8×4,026 operations to 4,026 per whole frame. It preserves the original
operation order for each pixel and adds no allocations.
