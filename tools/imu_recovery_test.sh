#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/imu-recovery
# Arduino's hardware headers are unavailable to native tests. Compile the exact
# sampling task with host I/O shims, not a second copy of its recovery logic.
python3 - <<'PY'
from pathlib import Path
source = Path("firmware/AttitudeIndicator/AttitudeIndicator.ino").read_text()
start = source.index("void samplingTask(void*) {")
end = source.index("\nvoid centeredText(", start)
Path("build/imu-recovery/imu_sampling_task.inc").write_text(source[start:end])
PY
"${CXX:-clang++}" -std=c++11 -Wall -Wextra -Werror \
  -fsanitize="${SANITIZER:-address,undefined}" \
  -Ifirmware/AttitudeIndicator -Ibuild/imu-recovery \
  tests/imu_recovery_test.cpp firmware/AttitudeIndicator/attitude.cpp \
  -o build/imu-recovery/test
UBSAN_OPTIONS=halt_on_error=1 build/imu-recovery/test
