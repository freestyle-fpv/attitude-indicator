# 统一按键校准

## 交互与数据

按默认安装方向（屏幕朝用户、USB 在左侧），右上角是 PWR，右下角是 BOOT。PWR 单击进入统一静止校准进度页，400 个连续稳定样本完成后返回所选表盘。移动会重新累计，校准中重复点击不会重新启动请求。触屏上下滑动仍负责循环切换仪表。

PWR 从 TCA9554 0x20 的 P4/EXIO4 读取，高为按下。设置 P4 输入和不反相时使用读改写，保留其他引脚配置；I2C 由采样任务统一访问，100Hz 检查按键。30ms 消抖，在松开时触发一次，小于800ms为单击；长按松开无动作。读取失败取消当前手势，启动时已按住不触发校准。

BOOT 原先的短按切换/长按演示运行时动作已删除。关闭 AXP2101 可配置的长按关机功能；断电后的硬件开机、上电 BOOT 下载模式仍属于芯片启动机制。

校准请求使用唯一编号，采样任务收到该请求并完成校准后才能退出进度页，旧的有效快照不会提前结束新校准。姿态滤波器统一保存陀螺仪偏置与安装参考四元数；姿态仪和 HUD 消费同一份校准后的 Snapshot。开始校准会关闭两者的姿态演示。空速/高度和航向仍为模拟源，IMU 校准不提供真实空速、高度或绝对航向。校准结果是当前运行会话内共享，开机仍自动校准。

## 验证

- 新增控制回归在旧代码上失败：单击原先切换表盘而非返回 Calibrate；修改后通过。
- `tools/calibration_test.sh`：点击消抖、只触发一次、长按、I2C失败、启动按住、时间回绕、请求编号与过期快照确认通过。
- `tools/airspeed_test.sh`：四种表盘单击校准保留模式，姿态和 HUD 演示同时关闭；模拟源、数值输入与显示策略测试通过。
- `tools/touch_test.sh`：上下滑动循环、方向与单次触摸锁存通过。
- 姿态滤波/安装归零/运动拒绝/稳定窗口/偏置估计20项测试通过。
- 固件编译成功：650,502字节程序，35,516字节静态RAM。自动烧录完成，写入哈希校验通过。
- 实板启动日志：`PWR calibration button EXIO4: ready; BOOT runtime actions disabled`。

用户已确认：“校准正常，已返回原表盘”。实板日志捕获一次实体 PWR 点击 (`pwr_clicks=1`)、`CALIBRATION start id=1` 和 `CALIBRATION complete id=1; shared pitch=0.00 roll=0.01`；随后空速表恢复，切到姿态仪仍为 `cal_id=1, valid=1`。校准期间采样约200Hz；日志中的校准界面约12FPS仅是临时进度页刷新，退出后恢复仪表帧率。

参考：[官方板级连接说明](https://raw.githubusercontent.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/main/HARDWARE_REFERENCE.md)、[官方板图](https://docs.waveshare.com/ESP32-S3-Touch-AMOLED-1.75)。

[90秒实板日志](shared-calibration-board-measurements.json) 还确认：HUD、姿态仪、空速表和高度计切换后的日志均携带同一 `cal_id=1`，没有再次校准，触摸错误为0；校准后剩余堆内存稳定37,052字节，没有panic/assert/watchdog记录。传感器依旧约200Hz，HUD防撕裂相位未改动。

最终固件SHA-256：`cce70683bf0dc83356d41d6b4ea7cff6cd552e46917afd8368a3ca6838994b4e`。
