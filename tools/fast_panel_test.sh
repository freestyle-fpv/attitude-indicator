#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
TASK_BUILD=build/fast-panel-regression
TASK_CXX=${CXX:-clang++}
mkdir -p "$TASK_BUILD"
compile_panel_test() {
  "$TASK_CXX" -std=c++11 -O2 -g -Wall -Wextra -Werror \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    -DESP32QSPI_MAX_PIXELS_AT_ONCE="$1" -Itests/fast_panel_stubs \
    -I"$2" -Ifirmware/AttitudeIndicator tests/fast_panel_test.cpp \
    "$2/fast_panel.cpp" -o "$3"
}
if [ "${1:-}" = --prove-regression ]; then
  # Mutate only an isolated copy: removing the hardware cap must reproduce the bug.
  mkdir -p "$TASK_BUILD/uncapped"
  cp firmware/AttitudeIndicator/fast_panel.cpp "$TASK_BUILD/uncapped/fast_panel.cpp"
  sed 's/static constexpr size_t ChunkPixels=.*/static constexpr size_t ChunkPixels=BusChunkPixels;/' \
    firmware/AttitudeIndicator/fast_panel.h > "$TASK_BUILD/uncapped/fast_panel.h"
  compile_panel_test 4096 "$TASK_BUILD/uncapped" "$TASK_BUILD/uncapped-test"
  if "$TASK_BUILD/uncapped-test" > "$TASK_BUILD/uncapped.log" 2>&1; then
    cat "$TASK_BUILD/uncapped.log"
    echo 'FAIL: removing the hardware DMA cap did not fail the regression' >&2
    exit 1
  fi
  cat "$TASK_BUILD/uncapped.log"
  rg -q 'DMA exceeds ESP32-S3 hardware limit' "$TASK_BUILD/uncapped.log"
  echo 'Uncapped 4096-pixel configuration rejected by hardware limit as expected (RED)'
elif [ "$#" -ne 0 ]; then
  echo "Usage: $0 [--prove-regression]" >&2
  exit 2
fi
for TASK_PIXELS in 1024 2048 4096; do
  compile_panel_test "$TASK_PIXELS" firmware/AttitudeIndicator "$TASK_BUILD/test-$TASK_PIXELS"
  "$TASK_BUILD/test-$TASK_PIXELS"
done
