#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p .tools build
if [ ! -x .tools/arduino-cli ]; then
  case "$(uname -s)-$(uname -m)" in
    Darwin-arm64) TASK_ARCH=macOS_ARM64;;
    Darwin-x86_64) TASK_ARCH=macOS_64bit;;
    Linux-x86_64) TASK_ARCH=Linux_64bit;;
    Linux-aarch64) TASK_ARCH=Linux_ARM64;;
    *) echo 'Install official arduino-cli as .tools/arduino-cli for this OS.' >&2; exit 2;;
  esac
  curl -L --fail --retry 3 "https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_${TASK_ARCH}.tar.gz" -o .tools/arduino-cli.tar.gz
  tar -xzf .tools/arduino-cli.tar.gz -C .tools arduino-cli
fi
.tools/arduino-cli version
python3 tools/fetch_dependencies.py
sh tools/arduino.sh core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
sh tools/arduino.sh core install esp32:esp32@3.3.10 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
