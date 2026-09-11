#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
${CXX:-clang++} -std=c++11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/AttitudeIndicator tests/touch_test.cpp firmware/AttitudeIndicator/instrument_controls.cpp -o build/touch_test
UBSAN_OPTIONS=halt_on_error=1 build/touch_test
