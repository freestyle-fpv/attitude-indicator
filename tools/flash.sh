#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
if [ "$#" -ne 1 ]; then
  echo 'Usage: sh tools/flash.sh SERIAL_PORT' >&2
  echo 'List ports: sh tools/arduino.sh board list' >&2
  exit 2
fi
if [ ! -f build/firmware/AttitudeIndicator.ino.bin ]; then
  echo 'Build first: sh tools/build.sh' >&2
  exit 2
fi
exec sh tools/arduino.sh upload --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc' --port "$1" --input-dir build/firmware --verify firmware/AttitudeIndicator
