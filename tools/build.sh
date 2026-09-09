#!/bin/sh
set -eu
TASK_ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$TASK_ROOT"
mkdir -p build/firmware
set -- compile --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc' --libraries vendor --warnings default --output-dir build/firmware firmware/AttitudeIndicator
# Arduino may reuse vendor objects after only a response-file flag changes.
# Rebuild them whenever the applied global options differ from this sketch.
if ! cmp -s firmware/AttitudeIndicator/build_opt.h build/applied-build-opt.h; then
  set -- "$@" --clean
fi
sh tools/arduino.sh "$@"
cp firmware/AttitudeIndicator/build_opt.h build/applied-build-opt.h
