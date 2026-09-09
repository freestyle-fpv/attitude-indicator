# Firmware review — 2026-09-09

Scope: `attitude.h`, `attitude.cpp`, `board_config.h`, `AttitudeIndicator.ino`, and `tests/attitude_test.cpp`. Sensor register behavior and scaling were compared with the checked-in `vendor/SensorLib/src/SensorQMI8658.hpp`. Review is of software behavior; physical mounting axes remain intentionally configurable and unverified. No firmware source was edited during review.

## Actionable findings

### P1 — Remove mounting trim on the correct side of the quaternion

**Location:** `firmware/AttitudeIndicator/attitude.cpp:53–55`, specifically `multiply(conjugate,q_,out)`.

The current relative rotation expresses the attitude in a tilted reference *world* frame. For mounting-tilt compensation, the sensor orientation is `q_sensor = q_aircraft * q_mount`, so recover the aircraft orientation using `q_sensor * conjugate(q_mount)`. The current order instead produces `conjugate(q_mount) * q_aircraft * q_mount`, coupling level yaw into displayed pitch and roll. The problem remains after gravity correction converges; it is not a transient fusion error.

**Executed reproduction:** initialize with acceleration `(0,-sin(20°),-cos(20°))`, call `zeroReference()`, then feed 200 samples at 0.005 s with that same acceleration and gyro `(0,(π/2)sin(20°),(π/2)cos(20°))`. These inputs represent a level aircraft yawing 90° in one second with a sensor mounting roll offset of 20°. Expected displayed pitch/roll are 0°/0°. Actual output is **20.000°/20.000°**, including after another ten seconds of stationary samples. This uses a hypothetical configurable mounting offset and does not assume the actual board axes.

**Fix:** remove the mounting quaternion on the right and add a combined mounting-tilt plus yaw regression. The existing trim test only verifies zero immediately after reference capture, so it cannot detect noncommuting rotations.

### P1 — Distinguish rejected sensor samples from ordinary not-ready polling

**Locations:** `firmware/AttitudeIndicator/AttitudeIndicator.ino:59–61,71–74` return the same `false` for transport failure, saturation, and no new data; `:129–132` waits for sample age above 100 ms before clearing calibration; `:206–207` continues rendering the attitude as valid during that interval.

A known invalid/clipped sample does not immediately invalidate the current attitude or its calibration. If accepted data resumes within 100 ms, the sample is silently skipped and `dt` spans the entire rejected interval. Integration then applies the *next* gyro reading to that interval, even though the motion during the gap was unmeasurable. The display can therefore retain valid status after losing a large rotation.

**Concrete branch reproduction:** after calibration and a valid sample at time T, return saturated gyro data on polls from T+5 through T+80 ms, then return an accepted stationary reading at T+85 ms. All rejected samples go through the same no-data branch. Their age never exceeds 100 ms, so `state.calibrated` remains true. The next accepted sample computes `dt=0.085`, passes the time-step gate, and integrates the new stationary gyro over the missing interval. Motion above the configured approximately 1024°/s limit for 80 ms could have changed orientation by over 80°. Display freshness remains true throughout, including after the gap. This reproduction follows the source control flow; it was not exercised on physical hardware.

**Fix:** return separate statuses for `not ready`, `valid sample`, and `invalid/fault`. Immediately invalidate the state on a detected transport error or saturation, and require the existing stationary recovery before displaying a calibrated attitude again. Ordinary no-new-data polls can retain the timeout behavior. Add a state-transition regression that injects a brief saturation or I2C failure between accepted samples.

## Verification and reviewed behavior

- Built and ran the native attitude test with `c++ -std=c++17 -Wall -Wextra -pedantic`: **15 checks passed, 0 failures**.
- Executed the additional mounting-tilt/yaw reproduction described above using the actual `attitude.cpp` implementation.
- Confirmed the configured scale factors, little-endian six-axis decoding, contiguous acceleration/gyro register block, and STATUS0 ready-bit masks against checked-in SensorLib source.
- Confirmed that the configured axis mapping has determinant +1 and is applied identically to acceleration and gyro. No physical orientation correction is proposed without a board measurement.
- Confirmed that snapshots and calibration commands cross the task boundary through FreeRTOS queues, and the IMU/I2C work is owned by the sampling task after setup.
- Confirmed that startup, missing sensor, and stale samples beyond the timeout lead to explicit status screens, while demo mode requires a user command.

Actual ESP32 firmware compilation is being performed independently by the parent task. Renderer review and hardware testing are outside this review's scope.

## Scoped re-review after fixes

**Final status: both findings addressed; no new actionable breakage found in the changed paths.**

- **Mounting trim addressed:** `attitude.cpp:57` now computes `q * conjugate(ref)`. Rebuilt and reran the original independent 20° mounting-offset / 90° yaw reproduction: displayed pitch/roll are now **−0.000°/−0.000°**, remaining level after ten seconds at rest. The permanent test suite includes the same combined-rotation regression.
- **Known sample faults addressed:** `readSample()` now distinguishes `NotReady`, `Valid`, and `Invalid`. Transport errors and saturation produce `Invalid`; the sampling task immediately clears calibrated state, resets the stationary window and integration timestamp, and publishes `sampleFault=true`. The display selects its unavailable screen. The next valid sample clears the fault presentation but starts a fresh full calibration window, preventing integration across the lost-motion interval. Ordinary no-data polling retains its freshness timeout, and persistent faults trigger reinitialization using either the previous sample time or the initial attempt time. These state transitions were reviewed in source; physical fault injection was not performed.
- Rebuilt and ran current native tests with `-std=c++17 -Wall -Wextra -pedantic`: **17 checks passed, 0 failures**. Correction to the initial review count above: the original suite had **16**, not 15, passing checks.

The scoped re-review edited only this report. ESP32 compilation remains the parent task's separate verification.

## Final bounded project review

Reviewed the final renderer and its tests; build, upload, setup, dependency-fetch and native-preview tools; preview HTML; and README alongside the previously reviewed firmware. **No additional concrete defects or unsafe automatic write behavior were found in this bounded review.** Both earlier findings remain addressed.

- Re-ran the existing native attitude and renderer executables: **17 attitude checks passed; renderer passed with 0 failures**. Renderer coverage includes circular clipping, complete lower perimeter, pitch and roll direction, fixed yellow aircraft/pointer, rotating white bank marks, pitch-independent outer annulus, inversion, wrap continuity, and frame boundary guards.
- Read the successful ESP32 compiler summary in `build/compile.log`: 453,574 bytes of program storage and 24,692 bytes of globals. Build output binaries are present in `build/firmware/`.
- Shell syntax checks passed for setup/build/upload/test wrappers. Python syntax parsing passed for the preview server and dependency fetcher.
- Build and upload use matching board options. Upload requires an explicit serial-port argument and enables verification; build, setup and preview do not automatically upload or erase a board.
- The preview binds only to loopback, validates finite/ranged angles, invokes the native renderer with an argument list, and writes frame output to a temporary directory. The browser controls remain separate from the sensor-driven board interface.
- README accurately distinguishes tested software from unverified hardware, explains the configurable physical-axis assumption, and documents the calibration and explicit demo controls.

Browser interaction verification and the final validation document are being completed by the parent task. No firmware was flashed or changed during this review; only this report was appended.


## FPS optimization integration review

The display task owns runtime panel writes. Two internal DMA bands are returned to the producer only after all queued SPI transactions complete; queued jobs carry generation and sequence identifiers so calibration/mode changes and sensor failures cannot replay the rest of an obsolete frame. Source and live sample freshness are checked before transmission. The FPS counter advances once per complete image.

SPI bus initialization is pinned to CPU0 so the vendor AUTO interrupt affinity matches the display consumer. This corrected a reproducible cross-core bus-lock ISR crash in the initial band experiment. Full-image and discard paths yield to the idle watchdog; a sustained calibration-screen test exceeded its five-second timeout without reset.

Wire-order renderer/FPS/DEMO output feeds DMA directly; the independent status overlay retains native RGB565 for the vendor drawing API. Four bands (118/118/118/112) require two 109,976-byte internal buffers. Independent integration review found no additional actionable defects. Native tests and hardware timing are documented separately in validation.md.


## Second FPS round review

Finite-coordinate floor/ceil helpers, tightened antialias support bounds and per-row strip clipping were reviewed. The rounding margins cover current small screen-space coordinates; shallow slopes and degenerate segments avoid strip division, and round caps remain inside the strip. Image regression tests confirm no new pixel differences. QueueDepth8 descriptor reuse was checked across repeated wraparound and the118/112-row tails; ownership, CPU0 affinity and watchdog handling remain intact. No actionable defects found.


## 第三轮FPS审查（2026-09-10）

独立审查覆盖原生窗口与缓存失效、圆形紧凑行索引/字节序/标签、三阶段FIFO缓冲所有权、背景只读并发、过期/故障/校准丢弃、内存失败清理与64KB分配储备。发现一次P1：增大库总线缓冲不能突破S3单次262144bit硬件上限。已用库容量与SPI_LL_DMA_MAX_BIT_LEN双重限制修复，build_opt统一2048；复查无其他可操作发现。

新增实际FastPanel.cpp的模拟SPI回归：1024/2048/4096三种库参数各51种显示带，共验证每组4467364字节；覆盖列/行窗口、第一段命令与后续裸数据、超过8个事务时FIFO描述符复用、CS/总线所有权和排空后归还。移除硬件限制的隔离副本确实重现失败，修正版三组ASan/UBSan通过。软件审查不替代实物稳定性；最终硬件记录见validation.md。


## 立体阴影与底部 FPS 审查（2026-09-10）

检查了预计算色表、指针投影缓存、下端支杆遮挡、圆形抗锯齿、内存分配和缓存覆盖顺序。发现一项 P2：半透明环口处 sky/ground 不相等，后画的白色地平线会直接覆盖阴影，形成亮点。已增加只读 8-bit 遮挡率缓存，使地平线透明度遵循环口与内阴影；原问题坐标的回归检查先失败后通过。独立复查的360度横滚扫描未发现残留亮点或原生/屏幕字节序差异。

FPS 标签移动到 (181,432)，104×22 像素矩形完整位于圆内，与 DEMO 分离。阴影/边缘操作按行稳定重排，保持每个像素的叠加先后顺序；空行、末行、紧凑分带边界及1,868字节索引检查通过。分带只遍历自己的操作，不再逐带扫描全部缓存。独立审查未发现其余可操作问题；收益以本轮实机日志为准。
