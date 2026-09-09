#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/renderer-circle-regression
TASK_CXX=${CXX:-clang++}
TASK_SANITIZER=${SANITIZER:-address,undefined}
TASK_FLAGS="-std=c++11 -O2 -pthread -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -DINSTRUMENT_TEST_WORD_COPY -DINSTRUMENT_TEST_POSE_CACHE -fsanitize=$TASK_SANITIZER"
"$TASK_CXX" $TASK_FLAGS -Dinstrument=renderer_circle_reference -c tests/fixtures/background_pose_reference/renderer.cpp -o build/renderer-circle-regression/reference.o
"$TASK_CXX" $TASK_FLAGS tests/renderer_circle_test.cpp firmware/AttitudeIndicator/renderer.cpp firmware/AttitudeIndicator/fps_overlay.cpp build/renderer-circle-regression/reference.o -o build/renderer-circle-regression/test
build/renderer-circle-regression/test
