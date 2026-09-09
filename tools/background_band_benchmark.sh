#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/background-band-benchmark-test
TASK_CXX=${CXX:-clang++}
TASK_FLAGS="-std=c++11 -O3 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -DINSTRUMENT_TEST_WORD_COPY"
"$TASK_CXX" $TASK_FLAGS -Dinstrument=background_reference -c tests/fixtures/background_band_benchmark_reference/renderer.cpp -o build/background-band-benchmark-test/reference.o
"$TASK_CXX" $TASK_FLAGS tests/background_band_benchmark.cpp firmware/AttitudeIndicator/renderer.cpp build/background-band-benchmark-test/reference.o -o build/background-band-benchmark-test/test
build/background-band-benchmark-test/test
