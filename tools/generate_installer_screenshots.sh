#!/bin/sh
# Generate clean carousel images from the actual firmware renderers (macOS).
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/installer-screenshots installer/screenshots
for dial in frame hud airspeed altimeter; do
  case "$dial" in frame) source=renderer;; *) source=${dial}_renderer;; esac
  "${CXX:-clang++}" -std=c++11 -O2 -Ifirmware/AttitudeIndicator "tools/render_${dial}.cpp" "firmware/AttitudeIndicator/${source}.cpp" firmware/AttitudeIndicator/fps_overlay.cpp firmware/AttitudeIndicator/dial_art.cpp -o "build/installer-screenshots/render_${dial}"
done
build/installer-screenshots/render_frame 0 0 build/installer-screenshots/attitude.bmp
build/installer-screenshots/render_hud build/installer-screenshots/hud.bmp 276 0 5 0 86 275 --no-fps
build/installer-screenshots/render_airspeed 86 build/installer-screenshots/airspeed.bmp
build/installer-screenshots/render_altimeter 275 build/installer-screenshots/altimeter.bmp --clean
for dial in attitude hud airspeed altimeter; do
  sips -s format png "build/installer-screenshots/$dial.bmp" --out "installer/screenshots/$dial.png" >/dev/null
done
