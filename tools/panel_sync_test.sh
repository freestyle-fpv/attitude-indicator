#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
${CXX:-clang++} -std=c++11 -Wall -Wextra -Werror -Itests/panel_sync_stubs -Ifirmware/AttitudeIndicator tests/panel_sync_test.cpp -o build/panel_sync_test
build/panel_sync_test
