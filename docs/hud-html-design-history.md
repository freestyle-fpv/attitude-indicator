# HUD HTML preview — design QA

Date: 2026-09-10

final result: passed (visual prototype and core interactions)

Scope: self-contained Canvas HTML demonstration. No HUD firmware integration or hardware performance claim.

## Sources and evidence

- Initial layout reference: `/var/folders/3k/6z3jp1cj09d59qt2r4rk_3km0000gn/T/codex-clipboard-8c4bf4d8-c2ce-40fa-ab2c-306aae0bd78a.png` (550 × 354).
- Fluorescent color and outlined-readout reference: `/var/folders/3k/6z3jp1cj09d59qt2r4rk_3km0000gn/T/codex-clipboard-c82e1a3f-a505-45ca-8906-40c620b85bc7.png`.
- Implementation: `preview/hud.html`, http://127.0.0.1:8766/hud.html.
- Initial normalized comparison captures: `preview/qa/hud-wide-550.png` (pitch 35°), `preview/qa/hud-round-466.png` (pitch 5°). Their source viewport captures and canvas bounds are recorded alongside them.
- Latest revision: `preview/qa/hud-neon-wide.jpg` (86 km/h, 275 m, heading 180°), `preview/qa/hud-neon-round-max.jpg` (200 km/h, 3000 m, heading 0°).
- Desktop viewport: 969 × 874 CSS pixels, DPR 2, document client width 954. Browser viewport images are delivered at 954 × 860 pixels. The first comparison normalized instrument-only crops to 550 × 354 and 466 × 466; latest screenshots show full-page context for both readout frames.
- Narrow viewport: 390 × 844 CSS pixels, DPR 1; `preview/qa/hud-mobile-browser.jpg`. Canvas width 347 CSS pixels, document width 375, no horizontal overflow.

## Visual review

References and browser captures were viewed together. The original horizontal tape layout remains; the second reference guides the bright green color and outlined current readings, without copying its additional combat-display information.

- Typography: narrow sans-serif scale labels; current readings and cardinal letters are heavier and larger. Labels remain legible at the 466-pixel round size.
- Layout and spacing: heading top, speed left, altitude right, pitch ladder center, bank bottom. Both current readings have a closed black-backed frame with an inward-facing tip aligned to their tape. Three-digit speed and four-digit altitude fit at the maximum values.
- Colors: the revised HUD uses `#00ff00` for all symbols, labels, outlines and optional soft glow, on pure black. Surrounding HTML controls retain their subdued palette.
- Assets: all moving symbols and scales are Canvas primitives, consistent with the user's code-drawn instrument requirement; no stock image is embedded as a display background.
- Copy: units remain km/h and metres, ranges 0–200 and 0–3000; SIM and manual/demo status distinguish the mock values. N/E/S/W mark 0°/90°/180°/270° on the scrolling heading tape.

Early visual issues were corrected: extreme heading/bank labels had narrow clipping bounds; current tape readings approached nearby major labels; round-layout NAV text crowded the bank scale. Bounds were expanded, nearby labels suppressed, and the round auxiliary text moved below the scale. Revised outlines and all four cardinal directions were checked in the browser after the changes. No unresolved visual blocker was found.

## Interaction review

- Sliders and numeric fields synchronize; altitude 3500 clamps to 3000, speed 250 clamps to 200.
- Heading 359° to 0° wraps; N, E, S and W were each visually checked at their matching heading.
- Manual changes and flight presets stop automatic demonstration; demo updates values and the instrument continuously.
- Pitch, roll and climb inputs update their corresponding display elements. Glow, declutter and reset controls work.
- Horizontal and circular layouts switch correctly; mobile controls remain available below the display.
- Fullscreen presentation and return using Escape were checked visually.
- Final browser warning/error log: empty.
- PNG export was invoked, but the in-app browser did not produce a downloadable-file event within 10 seconds. The download result remains unverified; this does not block the requested on-screen HTML demonstration.

Static code hygiene: `git diff --check` passes. No hardware changes were made as part of the HUD prototype.

## Revision: mechanical readout motion and clean lower display

The two outlined readings now use fixed-width digit columns. Each change briefly holds, then rolls only the changing digits vertically into the next detent (70 ms hold + 130 ms travel); increasing and decreasing values move in opposite directions. The surrounding frame stays fixed. Fast input changes are sampled after the current step completes, so obsolete intermediate values are not queued. The animation loop remains active until the last digit settles; reset synchronizes the counters immediately, and reduced-motion preference disables the rolling movement.

Removed SIM, NAV and load-factor text from the bottom of the HUD in both layouts. The lower bank scale is retained; simulation status remains outside the instrument in the HTML controls.

Browser checks: demo showed a stationary tens digit with a moving units digit inside the fixed frame; 99 → 100 and 999 → 1000 settled correctly in round mode, including the new leading digit. Screenshot: `preview/qa/hud-mechanical-round-carry.jpg`. Descent to zero, reset, and both layout modes were also checked. Final browser warning/error log was empty. `git diff --check` passed.
