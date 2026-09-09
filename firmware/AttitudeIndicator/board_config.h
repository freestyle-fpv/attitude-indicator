#pragma once
#include "attitude.h"
namespace board {
enum class SampleResult { NotReady, Valid, Invalid };
constexpr int Sda=15, Scl=14, LcdCs=12, LcdClock=38, LcdReset=39;
constexpr int LcdD0=4, LcdD1=5, LcdD2=6, LcdD3=7, Boot=0;
constexpr unsigned Width=466, Height=466;
constexpr unsigned LcdColumnOffset=6, LcdRowOffset=0;
constexpr uint8_t ImuAddress=0x6B, Brightness=180;
constexpr float RadPerDegree=0.01745329252f;
// Default installation: screen faces pilot, USB connector on the left.
// Measured sensor X points up, Y right and Z into the panel. Aircraft body coordinates
// are X into the panel, Y right, Z down. This is a proper rotation (det +1).
// Apply the same mapping to accelerometer and gyroscope samples.
inline instrument::Vec3 toBody(instrument::Vec3 sensor) {
  return {sensor.z, sensor.y, -sensor.x};
}
}
