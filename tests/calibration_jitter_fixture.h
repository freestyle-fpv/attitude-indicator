#pragma once
#include "../firmware/AttitudeIndicator/attitude.h"
#include <cmath>
namespace calibration_fixture {
constexpr float RadPerDegree=0.01745329252f;
inline instrument::Vec3 bias(){return {-.21f*RadPerDegree,3.45f*RadPerDegree,-4.01f*RadPerDegree};}
struct Sample {instrument::Vec3 acceleration,gyro;};
// Synthetic, balanced vibration at 200 Hz; ten complete cycles in 400 samples.
// It combines the reported 5.29 deg/s bias with bounded, zero-mean jitter.
inline Sample mild(unsigned i){
 const float phase=6.28318530718f*float(i%40)/40;
 const auto b=bias();
 return {{.025f*std::sin(phase),.015f*std::cos(phase),-1+.01f*std::sin(2*phase)},
         {b.x+.3f*RadPerDegree*std::sin(phase),b.y+1.2f*RadPerDegree*std::cos(phase),b.z+.2f*RadPerDegree*std::sin(2*phase)}};
}
}
