#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/background-test
TASK_CXX=${CXX:-clang++}
TASK_FLAGS="-std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator"
if [ "${WORD_COPY:-0}" = 1 ]; then
  TASK_FLAGS="$TASK_FLAGS -DINSTRUMENT_TEST_WORD_COPY"
fi
if [ "${LINE_ROUNDING:-1}" = 1 ]; then
  TASK_FLAGS="$TASK_FLAGS -DINSTRUMENT_TEST_LINE_ROUNDING"
fi
if [ "${SANITIZE:-1}" = 1 ]; then
  TASK_FLAGS="$TASK_FLAGS -fsanitize=address,undefined"
fi
# The original renderer is an independent, unchanged Q18 rendering oracle.
"$TASK_CXX" $TASK_FLAGS -Dinstrument=background_reference -c "${REFERENCE_RENDERER:-tests/fixtures/background_reference/renderer.cpp}" -o build/background-test/reference.o
"$TASK_CXX" $TASK_FLAGS -DINSTRUMENT_TEST_CACHE tests/background_comparison_test.cpp firmware/AttitudeIndicator/renderer.cpp build/background-test/reference.o -o build/background-test/comparison
build/background-test/comparison
