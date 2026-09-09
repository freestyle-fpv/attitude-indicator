# 航模座舱圆形姿态仪

为 **Waveshare ESP32-S3-Touch-AMOLED-1.75** 编写的独立姿态显示器。使用板载 QMI8658，UI 全部由 C++ 绘制，补全参考图的下半圆，以 466×466 原生分辨率显示。

![参考姿态预览](preview/reference.png)

## 当前交付

- 蓝天、棕地、球面渐变、白色刻度、黄色固定飞机标，完整正圆。内环采用方向性凹陷阴影与 4×4 边缘采样；黄色指针有柔和投影，支杆延伸到下缘并被环口遮住，已去掉底部梯形。
- 底部横向居中显示实际 FPS，约每秒更新；包含绘图、传屏和等待时间，首个统计窗口显示 `--.- FPS`。
- 内球面随俯仰/横滚运动；外圈地平线为直线，横滚刻度相对固定黄色指针旋转。
- 四元数姿态融合、开机静止校准、安装倾角归零、显式演示模式。
- IMU 与显示分别运行；读数异常、饱和或超时会停止显示有效姿态并重新校准。
- 本机交互预览直接运行固件的同一份 C++ renderer，不另写一套近似图形。
- 原生测试和 ESP32 固件编译通过；**已烧录实物并完成写入校验**。板载 IMU 已成功校准，持续输出有效姿态。性能优化采用圆形紧凑分带、内部几何缓存和背景/前景/DMA三阶段并行；增加立体阴影与内环遮挡后的当前配置静止姿态约 **42.8 FPS**，动态演示实测 **35.3–40.6 FPS**（中位37.2）（之前的性能版静止约42.9 FPS，原 FPS 标签版约8.5 FPS）。不同姿态仍会影响帧率。最新实测记录见 `docs/validation.md`。USB 日志采用非阻塞发送。默认 USB 左侧安装，重力轴向已实测核对。

## 查看预览

在项目目录执行：

```sh
sh tools/test.sh
python3 tools/preview.py
```

打开 <http://127.0.0.1:8765>。可调节俯仰/横滚、自动演示及保存 PNG。服务只监听本机。PNG 静态预览也保存在 `preview/reference.png`。

## 编译和烧录

当前电脑的编译环境已经安装在项目内，可直接执行：

```sh
sh tools/build.sh
sh tools/arduino.sh board list
sh tools/flash.sh /dev/cu.usbmodem21101
```

最后一条会向指定串口设备写入固件；把串口替换成开发板实际端口。该端口曾被识别为 ESP32 家族设备，但软件枚举不能确认具体微雪型号。写入前确认连接的是目标开发板，并根据需要保留原有程序。

构建输出在 `build/firmware/`：

| 文件 | 用途 |
| --- | --- |
| `AttitudeIndicator.ino.bin` | 应用程序，不能作为地址 0 的整包烧录 |
| `AttitudeIndicator.ino.merged.bin` | 合并固件；手动烧录时起始地址 **0x0** |
| `AttitudeIndicator.ino.bootloader.bin` | 引导程序，由上传脚本处理 |
| `AttitudeIndicator.ino.partitions.bin` | 分区表，由上传脚本处理 |

上传脚本使用各个固件文件的正确地址，并开启写入校验。没有自动执行擦除命令。

请保留 sketch 中的 `build_opt.h`，其中的全局参数把官方 QSPI 库容量设为 2048。构建脚本在该文件改变时自动完整重编译，防止复用不同参数的旧库；应用同时按芯片硬件上限限制 DMA 长度。

在新电脑上先安装 Python 3.9+、C++ 编译器和 curl，然后运行 `sh tools/setup.sh`。此脚本下载官方 Arduino CLI、固定版本的 ESP32 core 和微雪驱动；首次下载较大，可重复运行以复用已有文件。Arduino CLI 安装器采用官方 latest，当前验证使用的是 1.5.2-rc.1；核心与微雪库固定版本如下。

### 已验证构建配置

| 配置 | 值 |
| --- | --- |
| Arduino ESP32 core | **3.3.10** |
| Board | ESP32S3 Dev Module / `esp32:esp32:esp32s3` |
| Flash | **16 MB** |
| Partition | `app3M_fat9M_16MB` |
| PSRAM | **OPI PSRAM**（必须启用） |
| USB mode | Hardware CDC and JTAG / `hwcdc` |
| USB CDC on boot | Enabled / `cdc` |
| GFX | 微雪随包版本 1.6.4 |
| QSPI / DMA | 40 MHz；单次最多 16,384 个 RGB565 像素（32 KB） |
| 显示缓冲 | 三个 466×60 内部缓冲；每带直接生成圆形紧凑布局 |
| SensorLib | 微雪随包版本 0.3.1 |
| XPowersLib | 微雪随包版本 0.2.6 |
| 官方库固定提交 | `e4344e70c2fa78a13e8a06566507f1ba8af6672a` |

`tools/fetch_dependencies.py` 从官方提交下载，并用 Git blob SHA-1 核对每个文件。`vendor/SOURCE.json` 记录来源清单；上游许可证随库保留。不要直接替换成不同版本的 GFX/LVGL 示例库。

## 上电与操作

1. 把开发板安装在座舱方向，将模型放在希望定义为零姿态的位置，保持静止。
2. 上电后显示 `KEEP LEVEL & STILL / CALIBRATING`。需要 400 个连续稳定样本，通常数秒；晃动会重新计数。
3. 校准结束后显示仪表。抬头时内地平线向下；右机翼向下时地平线逆时针旋转。
4. **短按 BOOT**：重新估计陀螺仪零偏并将当前静止姿态作为零点。
5. **长按 BOOT 1.5 秒**：切换自动演示。演示状态明确显示 `DEMO`，不会因传感器故障自动进入。

运行时串口接受 `c`（校准）、`d`（演示切换），波特率 115200。串口每秒输出姿态、有效状态、校准计数、原始加速度、绘图/传屏耗时、最大帧间隔、采样率和剩余内存。日志使用零等待发送；电脑不读取时丢弃日志，仪表继续运行。BOOT 是启动绑带引脚：正常启动时松开按键。

## 安装方向

`firmware/AttitudeIndicator/board_config.h` 集中定义引脚、亮度和传感器轴映射。

默认安装方向是 **屏幕正对驾驶员、USB 接口在左侧**，黄色三角位于 12 点方向，画面无需额外旋转。实物静止读数核对：竖直时传感器 X 约 +1g，屏幕朝上平放时 Z 约 −0.97g。对应转换为航模 X 向前（进入面板）、Y 向右、Z 向下：

```cpp
return {sensor.z, sensor.y, -sensor.x};
```

该映射同时用于加速度和角速度，保持右手坐标系。上电前请恢复 USB 在左侧的竖直安装姿态，并将航模保持水平；如果在桌面朝上时已经自动校准，拿回安装方向后短按 BOOT 重新校准。静止归零补偿固定安装倾角。用户已在实物上确认抬头与右滚方向均正确。更换安装方向后应重新核对。

六轴融合会受振动和持续线性加速度影响，幅值门限只能抑制部分干扰。当前实现适合先做座舱显示及地面验证，持续转弯/高过载下的误差尚未测量。

## 文件结构

| 文件 | 作用 |
| --- | --- |
| `firmware/AttitudeIndicator/AttitudeIndicator.ino` | 硬件初始化、IMU 采样任务、状态与显示循环 |
| `firmware/AttitudeIndicator/attitude.cpp` | 四元数滤波、安装归零、静止校准 |
| `firmware/AttitudeIndicator/renderer.cpp` | 无贴图的完整圆形仪表、扫描线与几何缓存 |
| `firmware/AttitudeIndicator/display_pipeline.cpp` | 三缓冲背景/前景/DMA流水线、屏幕所有权、完整帧计数 |
| `firmware/AttitudeIndicator/fast_panel.cpp` | 40 MHz QSPI 队列 DMA 传输 |
| `firmware/AttitudeIndicator/fps_overlay.cpp` | 实测 FPS 与演示标记的代码字体 |
| `firmware/AttitudeIndicator/board_config.h` | 引脚和轴映射 |
| `firmware/AttitudeIndicator/circle_band.h` | 保住全部圆内像素的偶数列传输边界 |
| `firmware/AttitudeIndicator/build_opt.h` | 应用与官方显示库共用的编译参数 |
| `tests/` | 本机行为测试与内存边界验证 |
| `tools/render_frame.cpp` | 同一绘图核心的 BMP 导出工具 |
| `preview/index.html` / `tools/preview.py` | 本机预览界面及服务 |

## 官方依据

- [CO5300时序资料（第107页）](https://files.waveshare.com/wiki/common/Co5300_Datasheet.pdf#page=107)
- [Espressif CO5300像素传输实现](https://github.com/espressif/esp-iot-solution/blob/bd85f52467353dae3bd5f551054795287b0042a4/components/display/lcd/esp_lcd_co5300/esp_lcd_co5300_spi.c)

- [产品参数及外设说明](https://docs.waveshare.net/ESP32-S3-Touch-AMOLED-1.75/)
- [官方示例仓库](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75)
- [锁定版本硬件说明](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/HARDWARE_REFERENCE.md)
- [锁定版本显示示例](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75/blob/e4344e70c2fa78a13e8a06566507f1ba8af6672a/examples/arduino/01_HelloWorld/01_HelloWorld.ino)

软件验证详情见 `docs/validation.md`。


新增性能回归可单独运行：`sh tools/renderer_stages_test.sh`、`sh tools/renderer_circle_test.sh`、`sh tools/fast_panel_test.sh --prove-regression`。前两项可通过 `SANITIZER=thread` 检查跨线程访问，默认启用地址与未定义行为检查。串口 `render_ms` 是背景/前景经过时间之和，二者可以重叠；实际吞吐以 `fps` 和 `frame_ms` 为准。
