#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/background-pose-test
TASK_CXX=${CXX:-clang++}
TASK_FLAGS="-std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -DINSTRUMENT_TEST_WORD_COPY"
if [ "${SANITIZE:-1}" = 1 ]; then TASK_FLAGS="$TASK_FLAGS -fsanitize=address,undefined"; fi
"$TASK_CXX" $TASK_FLAGS -Dinstrument=background_reference -c tests/fixtures/background_pose_reference/renderer.cpp -o build/background-pose-test/reference.o
"$TASK_CXX" $TASK_FLAGS -DINSTRUMENT_TEST_POSE_CACHE -DINSTRUMENT_COMMAND_CAPACITY="${COMMAND_CAPACITY:-1024}" tests/background_pose_test.cpp firmware/AttitudeIndicator/renderer.cpp build/background-pose-test/reference.o -o build/background-pose-test/test
build/background-pose-test/test
