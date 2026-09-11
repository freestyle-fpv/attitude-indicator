#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
${CXX:-clang++} -std=c++11 -O2 -Wall -Wextra -Werror -pthread -fsanitize=${SANITIZER:-address,undefined} -Ifirmware/AttitudeIndicator tests/hud_test.cpp firmware/AttitudeIndicator/hud_renderer.cpp -o build/hud_test
UBSAN_OPTIONS=halt_on_error=1 build/hud_test
${CXX:-clang++} -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator tools/render_hud.cpp firmware/AttitudeIndicator/hud_renderer.cpp firmware/AttitudeIndicator/fps_overlay.cpp -o build/render_hud

${CXX:-clang++} -std=c++11 -O2 -Wall -Wextra -Werror -fsanitize=${SANITIZER:-address,undefined} -Ifirmware/AttitudeIndicator tests/hud_readout_test.cpp -o build/hud_readout_test
UBSAN_OPTIONS=halt_on_error=1 build/hud_readout_test
