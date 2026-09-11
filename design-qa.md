# Round HUD HTML — design QA

Date: 2026-09-10

final result: passed

Scope: the user approved all five recommendations from the current HUD review and requested a round-only HTML prototype. This change does not integrate or flash firmware.

## Latest revision: matching triangular indices and concentric center rings

Heading and bank now share a single 10px-wide, 10px-high outline triangle helper with a 1.25px stroke. Heading keeps its upward pointing orientation; bank rotates the same shape toward its own tick marks. Pointer tips and scale positions remain unchanged. The center marker adds a 5.5px radius inner circle concentric with the existing 10.5px outer circle.

Inline script syntax and triangle dimensions across five rotations passed. Browser default view confirms mirrored index shapes and separated concentric rings. Capture: `preview/qa/hud-matching-pointers.jpg`. Scope: HTML prototype and documentation.

## Earlier revision: smooth emphasis and cleaner side tapes

Replaced nearest-label switching with a continuous smoothstep influence based on distance to the heading/bank pointer. Size and brightness interpolate; regular and bold system-font faces crossfade at the same interpolated size. Additive weighted glyph coverage on the black background avoids dimming at the blend midpoint. Label glow is consistently disabled, avoiding a shadow change at a size threshold. Maximum size, reserved geometry and scale values remain unchanged.

Removed the small inward-facing triangles at both tape tops and the short inward horizontal stems at both readout windows. The existing readout frames remain.

Checked the actual script syntax and invoked its emphasis/render helpers: complementary transitions for 10° heading and 30° bank intervals, continuity across their midpoints, and constant summed glyph weights passed. Inspected the browser at heading 225° / bank 15°, where two neighboring labels share emphasis, and in automatic motion. No overlapping text or browser warning/error was observed. Capture: `preview/qa/hud-smooth-emphasis.jpg`. Changes remain within the HTML prototype and documentation.

## Earlier revision: centered tape labels and active scale emphasis

KM/H and M now share the horizontal centers of their 28px top caps (x = 71 and 395). Main readout wheels use an alphabetic baseline offset derived from the actual ascent and descent of the digit glyphs. This measurement is cached; all stationary and moving digits share the same ink-centered baseline, preserving the mechanical animation and its clip window.

The closest existing labeled graduation is emphasized at 16px / weight 700: heading every 10° (including N/E/S/W when applicable), bank at 0°, ±30° and ±60°. No standalone precise heading readout or extra graduation labels were added. The bank layout reserves the largest endpoint label in advance, maintaining fixed geometry as emphasis moves. Its 25 minor marks and all four scales' shared physical pitch remain; the reserved label space makes that pitch approximately 5.6px.

Validation compiled the actual inline script and invoked the actual geometry, heading, bank and counter/tape functions with drawing stubs. Ten heading states including 359°/0° and ten bank states checked one emphasized existing label, complete endpoint letters, all 25 bank marks and the bank text span. Counter ink centers and unit cap centers passed with controlled font metrics; actual font appearance was inspected in the browser.

Browser checks covered 200 km/h / 3000 m, heading 230°, bank +60°, and a 390 × 844 CSS viewport at heading 359° / bank −60°. The selected numbers, maximum-width readouts and cardinal labels were visible without overlap. The narrow canvas measured 347 × 347 inside a 375px document, with no horizontal overflow. Captures: `preview/qa/hud-label-emphasis.jpg`, `preview/qa/hud-label-emphasis-mobile.jpg`. Browser warning/error log was empty; viewport override reset. Scope remains HTML and documentation only.

## Earlier revision: matching density, complete roll subdivisions and V/S centering

Corrected the remaining spacing mismatch: previous heading marks were 14.1px apart, side marks 9.6px, and central roll marks approximately 6px with omitted marks near both ends. All minor divisions now share `g.tickPitch`, derived from a 5° step on the bank radius (approximately 6.03px). Heading divisions are 2°, speed 2 km/h, altitude 20 m, and bank 5°. Bank has all 25 marks from −60° through +60°; labeled major divisions retain their physical meaning. The heading label margin was adjusted to fit complete numerical labels between the fixed end letters.

V/S is centered on the 28px bottom cap of the right altitude tape, not on the left endpoint of that cap: x = (381 + 409) / 2 = 395. Both text rows share this anchor, moved 14px right. Its vertical placement remains unchanged.

Verification invoked the actual geometry, heading, side tape and bank functions with drawing stubs. Consecutive heading and side marks share the same physical pitch, all 25 bank bearings match −60° + 5° × index, and the V/S anchor matches the bottom-cap midpoint. Sampled eight headings including north wrap and cardinal/intercardinal directions, and side scales at zero/default/maximum values. Minor arc spacing and straight spacing use the same length; bank chord distance differs slightly due to curvature.

Browser checks covered the default view and a 390 × 844 CSS viewport at heading 359°, roll −60° and V/S −15.0. Full endpoint letters and V/S text remained visible, no overlaps were observed, and the browser warning/error log was empty. Captures: `preview/qa/hud-unified-density.jpg`, `preview/qa/hud-unified-density-mobile.jpg`. Viewport override reset; `git diff --check` passed. Final result: passed.

## Earlier revision: consistent four-axis ticks and lower V/S

All four scales now call the shared `scaleTick` helper: 14px major marks, 7px minor marks and 1.15px stroke width, matching the previous heading's regular long/short marks. Cardinal headings retain their highlight color and now use the same 14px major length. Bank retains 17 marks and its radial number alignment. Its previous intermediate length is replaced by the common short length; the pointer has 7px radial clearance from the longest mark.

V/S moves from beside the central altitude readout to directly below the right altitude axis. Both the signed value and V/S label are centered at x = 381, with baselines at y = 346 and 359. Verified the −15.0 value fits within the round display at desktop and narrow sizes.

Invoked the actual shared helper with vertical, both horizontal and radial unit directions, confirming 14/7px lengths and 1.15px strokes. Browser checks covered default state and roll +60° with V/S −15.0, including a 390 × 844 CSS viewport. Captures: `preview/qa/hud-unified-ticks.jpg` and `preview/qa/hud-unified-ticks-mobile.jpg`. No clipping or overlapping symbols observed. Browser warning/error log empty; viewport override reset; `git diff --check` passed. Final result: passed. This revision changes only the HTML prototype and its documentation.

## Earlier revision: roll detail and simplified pitch/center symbols

- Roll has 17 marks instead of 11, adding ±5°, ±15° and ±25°. Major marks lengthen from 13px to 16px; intermediate marks are 10px, fine marks 6px. The index triangle sits inside the marks with a 5px radial gap from the longest mark.
- Corrected the ±60 labels: their positions now follow the same radial directions as their marks, without horizontal clamping. The radius accounts for the actual label width so the total instrument, including numbers, remains 172.8px wide and aligned with the heading. The zero tick and zero label retain their vertical positions.
- Pitch lines have no vertical end caps. Negative pitch remains dashed. Removed the W-shaped line under the center marker; its ring, two wings and upper reference line remain.

Checked default state, pitch −8° with roll +60° and −60°, and a 390 × 844 CSS viewport. New captures: `preview/qa/hud-roll-pitch-clean.jpg`, `preview/qa/hud-roll-pitch-clean-mobile.jpg`. No mark/number/index collisions were visible. Browser warning/error log was empty and the viewport override was reset.

Invoked the extracted geometry, bank and marker functions with drawing stubs: 17 marks plus the index, ±60 labels at their corresponding polar angles, 172.8px total label bounds, and a ring with three central reference lines passed. Verified the pitch renderer contains only straight two-point segments. `git diff --check` passed. Final result: passed. Firmware remains unchanged.

## Earlier revision: persistent heading endpoints and matching roll width

Scope: apply the requested heading endpoint behavior and width adjustment to the restored polished HUD. Fonts, side tapes and the pitch ladder keep their restored implementation.

- Shared horizontal half-span changes from 72px to 86.4px, so the heading layout is 172.8px wide (1.2 times 144px). Tick spacing also increases by 1.2, retaining approximately the same angular view.
- Heading end letters remain anchored within the left/right bounds. They indicate the nearest cardinal outside the readable label band; they are directional hints, not the degree value of the tick underneath. Cardinal letters on visible ticks continue scrolling at their actual bearing. Examples: north shows W / N / E; 315° shows W at the left end and N at the right. Complete numerical labels are omitted when they would overlap the reserved end areas.
- Roll radius derives from the shared half-span and ±60° limits. Tick extremes match the heading bounds; outer numerical labels are clamped inside those bounds. The existing zero position and pointer angle behavior remain.

Verification: invoked the actual extracted heading function with lightweight drawing stubs over 3,600 headings at 0.1° intervals. Both endpoint letters were always valid and their anchors remained 172.8px apart; cardinal/intercardinal examples including 359° passed. Roll line extents at −60°, −30°, 0°, 30° and 60° were also 172.8px wide. This is a geometry/label check, not a device performance measurement.

Browser checks covered 180°, 359°, 0° and 315° headings, roll ±60°, and the 390 × 844 CSS viewport. The narrow canvas remained 347 × 347 inside a 375px document with no horizontal overflow. Both endpoint letters were visible and numerical labels did not overlap them. Desktop and narrow captures: `preview/qa/hud-heading-endpoints.jpg`, `preview/qa/hud-heading-endpoints-mobile.jpg`. Browser warning/error log was empty; viewport override reset; `git diff --check` passed. Final result: passed.

## Earlier revision: visual refinement

The requested polish retains the round-only, fluorescent-green HUD and the removed heading readout/baselines. Reference: `preview/qa/hud-clean-ticks.jpg`; final: `preview/qa/hud-polished.jpg`. These were inspected together at the same 969 × 874 CSS viewport and 72 km/h, 324 m, heading 210°, pitch 5°, roll −22°. Each browser image is 954 × 860 pixels. Page scroll differs slightly; the 466-pixel instrument is compared at matching scale rather than interpreting the header offset as a design change.

- Typography: instrument labels now share the same monospaced font stack as the mechanical values. Small labels render without glow to preserve crisp edges. Primary values remain 20px, with a lighter 500 weight.
- Spacing and layout: readout bodies now have equal 64px widths and mirrored tips, with subtle 2px corner bevels. Center wings and aircraft symbol are geometrically symmetric. Speed ticks use 2 km/h increments and 10 km/h labels, matching the physical minor/major tick spacing of the altitude tape.
- Color and stroke hierarchy: primary green remains #00ff00; secondary and minor colors are #00c600 and #008c00. Frame and symbol strokes are more consistent and finer. A restrained 0.9-logical-pixel glow is enabled initially and can be disabled.
- Edge treatment: the pitch ladder uses a cached 14px feather mask so strokes fade at the viewing boundary. Labels are still omitted as whole strings when they cannot fit; fitting labels fade in near the edge. Mechanical windows use shallow top/bottom shading while their frame stays stationary.
- Assets and copy: all graphics remain code-rendered Canvas geometry. No external assets, explanatory dial text, or removed baselines were introduced. Metric ranges, compact bank span and hysteresis behavior remain unchanged.

Checked default state, 200 km/h / 3000 m, +30° roll, −60° roll with −45° pitch, glow on/off, and a 390 × 844 narrow viewport. Narrow canvas measured 347 × 347 pixels inside a 375px document, without horizontal overflow. Evidence: `preview/qa/hud-polished-max.jpg` and `preview/qa/hud-polished-mobile.jpg`. The maximum capture precedes a final small-label glow adjustment that was then rechecked in the final desktop/narrow captures. Browser warning/error log was empty. `git diff --check` passed. No new P0/P1/P2 issue was found. Final result for this revision: passed.

The feather mask is rebuilt only on resize, and the pitch layer and digit-shading image are reused. Device FPS has not been measured for this HTML-only revision.

## Earlier revision: isolated ticks and compact bank scale

This revision supersedes the precise-heading and continuous bank-arc descriptions in the earlier review below. The standalone three-digit heading has been removed. The heading baseline and its end caps have been removed; the numbers, N/E/S/W, individual ticks and index triangle remain. The bank baseline arc has also been removed. Its tick radius decreased from 168 to 120 pixels, reducing tick-to-tick width by about 29%, while retaining the −60° to +60° range, angle mapping and bottom zero position.

Compared `preview/qa/hud-circle-optimized.jpg` with `preview/qa/hud-clean-ticks.jpg` together, at the same 969 × 874 CSS viewport, matching 72 km/h, 324 m, heading 210°, pitch 5° and roll −22°. Both captures are 954 × 860 pixels. Typography, bright-green hierarchy, frame layout and Canvas asset treatment remain consistent; only the requested heading readout, baselines and bank spacing changed. No new text collisions or clipped values were visible. Also checked zero and +60° bank positions. Browser warning/error log was empty and `git diff --check` passed. Final result for this revision: passed.

## Previous round-screen optimization review

### Target and evidence

- Primary source: `preview/qa/hud-review-round.jpg`, with the user's approved changes: whole labels at boundaries, larger primary values, fixed curved bank scale with a moving pointer, precise heading, and hysteresis for the mechanical readings.
- Original visual references: `/var/folders/3k/6z3jp1cj09d59qt2r4rk_3km0000gn/T/codex-clipboard-8c4bf4d8-c2ce-40fa-ab2c-306aae0bd78a.png` and `/var/folders/3k/6z3jp1cj09d59qt2r4rk_3km0000gn/T/codex-clipboard-c82e1a3f-a505-45ca-8906-40c620b85bc7.png`.
- Implementation: `preview/hud.html`, http://127.0.0.1:8766/hud.html.
- Final desktop capture: `preview/qa/hud-circle-optimized.jpg`.
- Maximum values and positive attitude limits: `preview/qa/hud-circle-max.jpg`.
- Narrow viewport capture: `preview/qa/hud-circle-mobile.jpg`.
- Earlier development history: `docs/hud-html-design-history.md`.

Desktop source and final captures are both 954 × 860 pixels from the 969 × 874 CSS viewport. The round instrument is 466 × 466 CSS pixels, with device density 2. The page scroll position differs, so instrument regions were compared at their matching displayed scale, without treating header position as a layout defect. Source state was a moving demo at approximately 72 km/h, 324 m, heading 210°, pitch 5°, roll −22°. The final comparison used those fixed values and −5.5 m/s. Minor source motion differences were excluded from fidelity judgments.

The source and final captures were displayed together in one comparison. The readout frames, heading and lower scale were large enough in these captures to inspect directly; a further magnified crop was unnecessary. The narrow viewport was 390 × 844 CSS pixels; the document width was 375 and the canvas measured 347 × 347, fully inside the page. No horizontal overflow was observed. Full-page capture from this provider was rejected because it incorrectly scaled the page; the accepted narrow image is a normal viewport screenshot.

## Findings and fixes

- [Resolved P2] In the earlier capture, a tape label at the upper edge was partially cut off, and rotated pitch labels could intersect the rectangular drawing boundary. Tape labels now require a full vertical margin. Pitch label corner bounds are transformed with the current roll and checked before drawing; incomplete labels are omitted as whole strings. Scale strokes may still end at the viewing boundary, by design.
- [Resolved P2] Main readings competed with the minor scale. Their monospaced digits increased from 16 to 20 logical pixels, frames widened to accommodate three/four digits, and primary symbols remain `#00ff00` while secondary and minor marks use `#00b800` / `#008f00`.
- [Approved layout revision] The horizontal mode and its controls, code branches, fullscreen ratio and export filename were removed. The instrument stays circular in normal and fullscreen presentations.
- [Approved layout revision] The bank scale is now a fixed lower arc covering −60° through +60°, with labelled 0°, ±30° and ±60° and intermediate ticks. The bright triangle moves to the corresponding roll angle.
- [Approved layout revision] A three-digit precise heading such as `210°` appears above the N/E/S/W tape and wraps from 359° to 000°.
- [Approved behavior revision] The mechanical counters use a 0.65-unit change threshold, providing a 0.30-unit hysteresis band around each half-unit rounding boundary. Existing hold/roll timing remains 70/130 ms. Updated readings are sampled after the current movement finishes, without queuing obsolete values.

## Required visual surfaces

- Typography: larger, fixed-width primary values fit at 200 km/h and 3000 m; smaller heading, pitch and bank labels remain legible. Boundaries suppress whole labels rather than truncate glyphs.
- Spacing/layout: circular face, lower arc and enlarged frames remain inside the circle; the heading readout is separate from tape labels. Desktop controls remain at the right, narrow controls stack below.
- Colors: primary green remains fluorescent; secondary levels establish a clear hierarchy. Black background and optional glow are retained.
- Assets: live instrument shapes continue to use Canvas primitives per the user's code-drawn UI requirement. No raster replacement or image asset is needed.
- Copy: only round-screen layout copy remains. Metric units and the 0–200 km/h / 0–3000 m ranges are unchanged. Previously removed bottom SIM/NAV/load-factor text remains absent from the instrument.

## Verification

- Browser loaded without warning/error logs; final log check was empty.
- Checked maximum readings, roll +60°/−60°, pitch +45°/−45°, heading 359°/0°, default state and a representative −22° turn.
- Fullscreen renders the circular instrument correctly. Automated Escape did not consistently exit the in-app browser fullscreen surface; reloading returned to the controls. Native Escape behavior is not asserted by this review.
- The narrow viewport remained square with no horizontal overflow; temporary viewport override was reset.
- Ran the actual `stableReading` function extracted from the HTML against 86.49, 86.51, 86.48, 86.56, 86.66, 86.52, 86.44, 86.34. The readings were 86, 86, 86, 86, 87, 87, 87, 86: small boundary noise held steady and larger changes updated in both directions. Zero, 3000 and large changes also passed.
- Checked demo, manual input and reset during the review. `git diff --check` passed.
- The prior PNG-download verification gap remains: filename and square export code were updated, but the in-app browser download result is not claimed.

No actionable P0/P1/P2 visual findings remain. Actual small-screen readability and device FPS require later firmware implementation and on-board measurement.


## 2026-09-10 — physical-display readability update

Enlarged primary values 20→26px, side labels 11→14px, selected marks to 19px, shared horizontal span to 208px, major/minor ticks to 17/9px and stroke to 1.5px. Pointer triangles are 12px and center circles 14/7.5px. Numeric windows and mechanical travel increased to fit. The corresponding native renderer uses the same geometry.

Reloaded the browser preview and visually checked default values, then 200 km/h, 3000 m, heading 359° and roll −60°. Maximum readings fit the windows; cardinal labels and bank labels remain within the circular display. Screenshot: `preview/qa/hud-large-html-max.jpg`. Browser warning/error log was empty. Native default and maximum renders were independently inspected; 2,000 randomized native frames passed seven-region coverage checks. This update has been flashed; see `docs/hud-firmware-validation.md` for device performance and remaining limits.


### Mechanical counter cadence fix

Removed the 70ms hold followed by a 130ms cubic burst. Both HTML and firmware now use 240ms smoothstep travel and retain leftover frame time between transitions. At 63ms simulated frame intervals the native test previously held the first frame; now four successive frames move and then settle. Browser preview reloaded with 87 km/h and 276 m, with no warning/error logs. This addresses animation cadence, not the underlying 16–17 FPS renderer limit.


## 2026-09-10 — sparse scales, larger crisp labels

Reduced minor tick density: heading 2→5°, speed 2→5km/h, altitude 20→50m, roll 5→10°. Four perimeter scales retain matching physical spacing (~13px) and 17/9px mark lengths. Side values become 18px, heading/roll 20–22px, pitch 20px. Full brightness and stronger baseline font weight avoid faint peripheral bank labels. Cardinal-adjacent redundant heading numbers are omitted to prevent large labels colliding.

Pitch uses alternating long 10° / short 5° marks with 90px spacing. Zero uses the same long-line width. Long and short half-spans are 72px/56px with a 42px central gap; negative pitch uses two balanced dash segments with matching total spans. Only long marks have numbers. Pitch lines retain edge fade; numbers are drawn after the mask in HTML (with fade disabled in native code), at full brightness only when the complete label fits.

Checked default and heading359/roll−60/pitch−8/200km/h/3000m native frames. Reloaded the HTML and visually checked the matching default layout with no browser warning/error logs. ASan/UBSan covers equal 0°/10° long-line geometry, shorter 5° marks, full-bright edge label then whole-label removal, and existing animation/region reconstruction. Randomized 2,000 frames remain inside seven damage bands (163,972 pixels). Device results are recorded in `docs/hud-sparse-validation.md`.


## 2026-09-10 — continuous, fully labelled pitch ladder

Height tape now has 5m minor / 20m major intervals. Pitch rows move to 60px spacing (12px/degree), retaining 5° divisions and alternating 144px/112px long/short widths. Every row has 20px numbers. All pitch lines are solid and continuous through the middle, with negative values identified by minus signs; the central logo overlays the continuous line. Whole-label edge culling remains to avoid clipped digits.

Native ASan/UBSan verifies continuous long lines, the shorter intermediate line and its visible label, plus existing wheel, buffer, wire and boundary checks. Default native and HTML images were visually inspected. Browser warning/error log empty. This intermediate solid-line design was superseded below; final validation is in docs/hud-hooked-validation.md.


## 2026-09-10 — dashed negative pitch, hooks and accurate label bounds

Restored negative-pitch 8px dashes / 5px spaces, phased with a dash across the central axis; positives remain solid. Added 6px end hooks toward the horizon. Doubled the long/short width difference: total widths 160px/96px. All 5° rows remain numbered, with 60px spacing, and the height scale remains 5m minor / 20m major.

Expanded pitch clipping from x=112–354 to x=102–364; reduced label separation from 7px to 4px. The font generator now precomputes glyph ink bounds from both atlas weights, with bilinear support added at render preparation; the renderer rotates individual glyph bounds for visibility checks. Browser uses actualBoundingBox metrics. This avoids hiding entire labels based on the old oversized 28px-high rectangle and unused atlas margins. Labels still disappear as a whole if the actual ink would be clipped. Compared default and pitch−8°/roll−30° views in native and browser outputs: previously suppressed left −10 and −15 now appear.

A narrow row interval in the line rasterizer avoids evaluating empty pixels inside large tilted-line bounding boxes. Before the subsequent hook/dash edits, 500 full-frame hash comparisons (108,578,000 pixels, including near-horizontal cases) were identical. Final host sanitizer tests and 2,000-frame damage/circle checks passed; final hardware measurements are in docs/hud-hooked-validation.md. Screenshots: preview/qa/hud-native-hooked.png, hud-native-hooked-bank.png and hud-hooked-html.jpg.


## 2026-09-10：循环滑动与 FPS 覆盖层

- 航向和横滚指针附近刻度以平滑权重加粗至 3.2px。
- 按最新要求恢复横滚原来的 y=420 轴线，FPS 在底部 y=443 叠加，小黑底覆盖下层图形，不为其保留布局空间。
- HTML 已刷新并截图，无浏览器警告/错误；固件同样保持原布局。
- 开发板上滑下一表盘、下滑上一表盘，四模式首尾循环；详见 docs/hud-swipe-validation.md。

- 按最终反馈统一为现有 FpsOverlay：白色 5×7 像素字形、2 倍绘制、深灰底、x=181/y=432/104×22；在 display consumer 中绘制，不占用 HUD 布局。HTML 使用相同字形数据。上、下滑动方向及四模式循环已经用户实板确认。


## 2026-09-10：HUD 性能优化后的最终读数行为

- 整组精确读数按 0.4s 停留、0.3s 平滑滚动；数字增加向下滚动，减少反向。合并为最新目标，保留可读停留，不逐位翻动、不要求与刻度轴严格同速。
- 短行程随动已取消。左右数字为 26px，停留时居中。
- 侧轴数字在读数框附近、轴上下端和圆形边界渐隐；航向数字靠近端点渐隐。远离边界保持清晰，俯仰字形规则保持。
- 优化及实际硬件 FPS 见 docs/hud-performance-validation.md；浏览器 FPS 不能代表开发板性能。


## 2026-09-10：按需数位、无停顿随轴滚动（当前版本）

- 仅发生变化的数位滚动：128→129 一位、129→130 两位、199→200 三位、999→1000 四位；所有相同字符位置固定。进位和退位都覆盖。
- 用户最新要求取消停顿和定时换位。使用轴值的整数/小数部分直接决定相邻数字和滚动位置，无缓动追赶；输入停住后数位也停在对应位置。前述 0.4s/0.3s 或 0.2–0.6s 限时方案均已被替代。
- HTML 与固件同步；浏览器演示已检查，无警告/错误。主机覆盖静止输入、连续相位、进位/退位、变化区域重建、槽位隔离、DMA 生命周期。
- 实板帧率与最终验证见 docs/hud-40-validation.md；浏览器 FPS 不代表开发板 FPS。
