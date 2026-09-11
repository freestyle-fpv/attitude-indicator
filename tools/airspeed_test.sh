#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build
TASK_CXX=${CXX:-c++}
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/airspeed_test.cpp firmware/AttitudeIndicator/airspeed_renderer.cpp firmware/AttitudeIndicator/dial_art.cpp -o build/airspeed_test
build/airspeed_test
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/instrument_controls_test.cpp firmware/AttitudeIndicator/instrument_controls.cpp -o build/instrument_controls_test
build/instrument_controls_test
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator -fsanitize=address,undefined tests/display_policy_test.cpp -o build/display_policy_test
build/display_policy_test
"$TASK_CXX" -std=c++11 -O2 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator tools/render_airspeed.cpp firmware/AttitudeIndicator/airspeed_renderer.cpp firmware/AttitudeIndicator/dial_art.cpp -o build/render_airspeed
