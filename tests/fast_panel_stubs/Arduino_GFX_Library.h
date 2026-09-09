#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#ifndef ESP32QSPI_MAX_PIXELS_AT_ONCE
#define ESP32QSPI_MAX_PIXELS_AT_ONCE 1024
#endif
uint32_t micros();
// Keep PanelDisplay itself real; only the hardware-backed vendor base is fake.
class Arduino_CO5300 {
 protected:
  int16_t _currentX=0, _currentY=0;
};
