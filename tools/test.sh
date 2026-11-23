#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build
node tests/installer_loading_test.js
node --test tests/web_diagnostics_test.js
python3 tests/installer_packaging_test.py
TASK_CXX=${CXX:-clang++}
"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/attitude_test.cpp firmware/AttitudeIndicator/attitude.cpp -o build/attitude_test
build/attitude_test
"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined tests/calibration_store_test.cpp firmware/AttitudeIndicator/attitude.cpp -o build/calibration_store_test
build/calibration_store_test
"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/AttitudeIndicator tests/hardware_diagnostics_test.cpp -o build/hardware_diagnostics_test
build/hardware_diagnostics_test
sh tools/imu_recovery_test.sh
"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/renderer_test.cpp firmware/AttitudeIndicator/renderer.cpp -o build/renderer_test
build/renderer_test
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/depth_style_test.cpp firmware/AttitudeIndicator/renderer.cpp -o build/depth_style_test
build/depth_style_test
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Ifirmware/AttitudeIndicator tools/render_frame.cpp firmware/AttitudeIndicator/renderer.cpp firmware/AttitudeIndicator/fps_overlay.cpp -o build/render_frame

"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/diagnostics_test.cpp -o build/diagnostics_test
build/diagnostics_test
"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/imu_diagnostics_test.cpp -o build/imu_diagnostics_test
build/imu_diagnostics_test

"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/board_test.cpp firmware/AttitudeIndicator/attitude.cpp -o build/board_test
build/board_test

"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/fps_test.cpp firmware/AttitudeIndicator/fps_overlay.cpp -o build/fps_test
build/fps_test

"$TASK_CXX" -std=c++11 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/transport_test.cpp -o build/transport_test
build/transport_test

"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/raster_math_test.cpp -o build/raster_math_test
build/raster_math_test

"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/circle_band_test.cpp firmware/AttitudeIndicator/renderer.cpp firmware/AttitudeIndicator/fps_overlay.cpp -o build/circle_band_test
build/circle_band_test
