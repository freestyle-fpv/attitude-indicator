#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
export ARDUINO_DIRECTORIES_DATA="$TASK_ROOT/.arduino/data"
export ARDUINO_DIRECTORIES_DOWNLOADS="$TASK_ROOT/.arduino/downloads"
export ARDUINO_DIRECTORIES_USER="$TASK_ROOT/.arduino/user"
export ARDUINO_BUILD_CACHE_PATH="$TASK_ROOT/.arduino/build-cache"
exec "$TASK_ROOT/.tools/arduino-cli" "$@"
