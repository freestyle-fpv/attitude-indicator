#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build
# Compile both renderers with the same host compiler so float rounding agrees.
for version in reference current; do
  if [ "$version" = reference ]; then
    source=tests/fixtures/hud_raster_reference/hud_renderer.cpp
  else
    source=firmware/AttitudeIndicator/hud_renderer.cpp
  fi
  ${CXX:-clang++} -std=c++11 -O3 -Wall -Wextra -Werror -Ifirmware/AttitudeIndicator tests/hud_pixel_hash.cpp "$source" -o "build/hud-pixels-$version"
  "build/hud-pixels-$version" --exclude-requested-ui-changes > "build/hud-pixels-$version.txt"
done
cmp build/hud-pixels-reference.txt build/hud-pixels-current.txt
printf 'PASS 500 frames match the pre-optimization rasterizer outside the intentionally redesigned readouts and fading scale labels\n'
