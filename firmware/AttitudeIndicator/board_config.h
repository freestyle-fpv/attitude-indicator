#pragma once
#include "attitude.h"
#include "installation.h"
namespace board {
enum class SampleResult { NotReady, Valid, Invalid };
constexpr int Sda=15, Scl=14, LcdCs=12, LcdClock=38, LcdReset=39, LcdTe=13;
constexpr int LcdD0=4, LcdD1=5, LcdD2=6, LcdD3=7, Boot=0;
constexpr int TouchReset=40,TouchInterrupt=11;
constexpr uint8_t TouchAddress=0x5A,ExpanderAddress=0x20;
constexpr uint8_t PowerButtonMask=1u<<4; // PWR/SYS_OUT on TCA9554 P4, active high.
constexpr unsigned Width=466, Height=466;
constexpr unsigned LcdColumnOffset=6, LcdRowOffset=0;
constexpr uint8_t ImuAddress=0x6B, Brightness=180;
constexpr float RadPerDegree=0.01745329252f;
// The CST9217 origin is opposite the native panel origin on both axes.
constexpr int touchScreenY(int rawY){return int(Height)-1-rawY;}
inline instrument::ScreenPoint touchPoint(int rawX,int rawY,instrument::UsbSide side){
  return instrument::toPanel({int(Width)-1-rawX,touchScreenY(rawY)},instrument::inverse(side));
}
// Default installation: screen faces pilot, USB connector on the left.
// Measured sensor X points up, Y right and Z into the panel. Aircraft body coordinates
// are X into the panel, Y right, Z down. This is a proper rotation (det +1).
// Apply the same mapping to accelerometer and gyroscope samples.
inline instrument::Vec3 toBody(instrument::Vec3 sensor,instrument::UsbSide side=instrument::UsbSide::Left) {
  using instrument::UsbSide;
  switch(side){
    case UsbSide::Top:return {sensor.z,sensor.x,sensor.y};
    case UsbSide::Right:return {sensor.z,-sensor.y,sensor.x};
    case UsbSide::Bottom:return {sensor.z,-sensor.x,-sensor.y};
    default:return {sensor.z,sensor.y,-sensor.x};
  }
}
}
