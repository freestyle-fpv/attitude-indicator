#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/background-wire-test
TASK_CXX=${CXX:-clang++}
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -DINSTRUMENT_TEST_WORD_COPY -fsanitize=address,undefined tests/background_wire_test.cpp firmware/AttitudeIndicator/renderer.cpp firmware/AttitudeIndicator/fps_overlay.cpp -o build/background-wire-test/test
build/background-wire-test/test
