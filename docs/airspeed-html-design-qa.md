# Airspeed HTML preview — design QA

Date: 2026-09-10

final result: passed

Scope: browser prototype for visual approval, with a provisional 0–300 km/h scale. Hardware rendering and flight-speed thresholds are outside this review.

## Evidence

- Source: `/var/folders/3k/6z3jp1cj09d59qt2r4rk_3km0000gn/T/codex-clipboard-9201544a-5b12-4e19-a70f-ce71c913528e.png`
- Implementation: `preview/airspeed.html`
- Desktop screenshot: `preview/qa/airspeed-desktop.png`
- Desktop viewport: 969 × 874 CSS pixels, device pixel ratio 2, canvas display 539 × 539. Captured at 160 km/h, manual mode, color bands off.
- Narrow viewport: 390 × 844; visible canvas 331 × 331. No horizontal overflow; controls stack below the dial.

## Visual review

The source and implementation were displayed together for comparison. The round near-black face, cream bold typography, rounded tick ends, dark gray inner disc, tapered hand, and metallic center/counterweight preserve the reference style. The source's multiple altimeter hands, ALT/FEET labels, and striped area are intentionally replaced by a single airspeed hand and AIR SPEED/km/h labels.

Three-digit labels initially approached the ticks. Moving their radius from 159 to 145 logical pixels resolved that collision. Moving km/h from y=287 to y=317 cleared the counterweight in the initial pose. Label spacing, circle boundaries, cream/black contrast, control copy, and needle rendering were visually checked after these changes. Pointer overlap with a scale label during rotation is intentional mechanical-instrument behavior.

## Interaction review

- Presets, numeric input, and slider keyboard adjustment synchronize the displayed value and accessible canvas description.
- Tested 0, 250, and 300 km/h and a 135 → 136 keyboard increment.
- Automatic demonstration advances the value; pause returns to manual mode.
- Color-band toggle and 466 px/enlarged size controls update the preview.
- Narrow-layout controls remain available below the dial.
- Browser warning/error log was empty during review.

The PNG export handler was inspected but its browser download was not tested. User approval of the style, range, and optional bands remains pending.

## Revision: continuous edge bands

On 2026-09-10, the bands were widened from 5 to 12 logical pixels, with their outer edge aligned to the dial radius of 233. Green now spans 0–200, yellow 200–260, and red 260–300 km/h; these remain illustrative thresholds. Bands are enabled initially so the revised appearance is visible immediately.

Verified in the browser at 969 × 874 and 135 km/h: the bands reach the circular edge, cover the entire marked speed range, meet without visible gaps, and remain separate from the ticks. Browser warning/error log was empty. Screenshot: `preview/qa/airspeed-bands-edge.png`. Final result for this revision: passed.

## Revision: 0–200 km/h range

The scale, pointer mapping, inputs, presets, accessible description, footer, and demo now use a maximum of 200 km/h. The 270-degree sweep and edge-band width remain unchanged. Example bands are green 0–140, yellow 140–180, and red 180–200. Major labels remain spaced every 50 km/h, minor ticks every 10.

Verified that entering 250 clamps both controls and the description to 200; visually confirmed the pointer at the 200 endpoint. The 120 preset was checked and captured in `preview/qa/airspeed-200.png` at 969 × 874. Labels and bands remain clear; browser warning/error log was empty. Final result for this revision: passed.

## Revision: denser numbers and pointer shadow

Major labels now appear every 20 km/h, with intermediate ticks every 5 km/h. Labels use bold 34-pixel type at radius 148 to fit all eleven numbers. Pointer and counterweight shadows are darker and offset down/right; the center cap also casts a shadow. Shadow values scale with backing-pixel density to preserve their apparent depth.

Visually verified enlarged and 466-pixel modes at 135 km/h: numbers do not collide with each other or the ticks, and the needle casts a visible soft shadow on the face. The pointer may cover the number it indicates, as expected. Browser warning/error log was empty. Screenshot: `preview/qa/airspeed-dense-shadow.png`. Final result for this revision: passed.
