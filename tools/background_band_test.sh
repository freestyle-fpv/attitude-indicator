#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/background-band-test
TASK_CXX=${CXX:-clang++}
TASK_FLAGS="-std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -DINSTRUMENT_TEST_WORD_COPY -fsanitize=address,undefined"
"$TASK_CXX" $TASK_FLAGS -Dinstrument=background_reference -c tests/fixtures/background_band_reference/renderer.cpp -o build/background-band-test/reference.o
"$TASK_CXX" $TASK_FLAGS tests/background_band_test.cpp firmware/AttitudeIndicator/renderer.cpp firmware/AttitudeIndicator/fps_overlay.cpp build/background-band-test/reference.o -o build/background-band-test/test
build/background-band-test/test
