#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build
TASK_CXX=${CXX:-c++}
TASK_SANITIZER=${SANITIZER:-address,undefined}
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -pthread -fsanitize="$TASK_SANITIZER" tests/altimeter_test.cpp firmware/AttitudeIndicator/altimeter_renderer.cpp firmware/AttitudeIndicator/dial_art.cpp -o build/altimeter_test
build/altimeter_test
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator tools/render_altimeter.cpp firmware/AttitudeIndicator/altimeter_renderer.cpp firmware/AttitudeIndicator/dial_art.cpp firmware/AttitudeIndicator/fps_overlay.cpp -o build/render_altimeter
