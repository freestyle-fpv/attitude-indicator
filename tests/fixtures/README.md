# Rendering oracles

These fixtures retain earlier implementations of the pixel rasterizer, band
renderer and pose handling. Tests compare their results with the current
optimized implementation, including forced allocation and command-cache failure.

The September 10 depth styling revision deliberately shares only the artwork
definitions in `instrument_style.h`: fixed geometry and the precomputed sky/ground
palette. The old pedestal and hard rim are no longer the expected artwork. The
fixtures' rasterization, clipping, byte ordering and replay paths were left
intact. Each engine additionally stores the paint's 8-bit horizon transmittance
and scales its independently computed horizon alpha by it; this explicit paint
contract prevents the horizon from overwriting the partially covered rim. Sharing artwork does not independently verify its visual
design; `depth_style_test.cpp` checks the requested transparency and shadow
behavior, and rendered native-resolution previews provide visual review.

The FPV readability revision also shares the pitch and bank artwork and
horizon width. The original scalar shader still paints the horizon per pixel,
independently of the current renderer's span calculation.
