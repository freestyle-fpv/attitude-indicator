#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CXX:-clang++} -std=c++11 -Wall -Wextra -Werror -fsanitize=address,undefined -Ifirmware/AttitudeIndicator tests/calibration_controls_test.cpp -o build/calibration_controls_test
build/calibration_controls_test
