# HUD tick emphasis, FPS and vertical swipe plan

Goal: emphasize pointer-aligned ticks, show real presentation FPS and cycle all four instruments with vertical gestures.
Architecture: portable CST9217 packet parser and single-touch vertical recognizer; I2C transactions stay in the existing sampling task and only directional events cross a queue to UI controls. Preserve generation invalidation and full-screen restore on switching. No additional framebuffer or task.

- [x] Add tested up/down wraparound mode selection and a portable gesture/parser module. Reject horizontal drags, taps, multi-touch, stale/error packets and repeat triggers during one contact.
- [x] Integrate reset/probe and exact-length D000 report/AB ACK using the official 0x5A, reset40, IRQ11 wiring. Poll at 50Hz on sampling task; back off after failures. Record touch diagnostics and directional switch events.
- [x] Emphasize heading/bank ticks smoothly (1.5–3.2px). Keep bank at its original position; overlay bottom-center FPS without reserving layout space; reuse the existing FpsOverlay on the display consumer after rendering, without extra HUD glyphs or frame state.
- [x] Synchronize the HUD HTML emphasis/FPS layout. Browser FPS measures preview frames, not device FPS.
- [x] Run host gesture/parser/control and rendering tests, compile, flash under existing automatic-flash authorization, inspect touch presence and sampling/FPS logs. Ask user to perform one up/down gesture for physical direction verification after implementation is on board.
- [x] Document actual validation and any unresolved physical checks; leave HUD running with real IMU attitude. No commit/push requested.
