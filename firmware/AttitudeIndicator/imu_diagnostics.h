#pragma once
#include "runtime_state.h"
#include "firmware_version.h"
#include <cmath>
#include <cstdio>

namespace instrument {
inline const char* calibrationReasonName(StationaryCalibration::Rejection reason){
  using Rejection=StationaryCalibration::Rejection;
  switch(reason){
    case Rejection::Nonfinite:return "nonfinite";
    case Rejection::GravityRange:return "gravity_range";
    case Rejection::GyroRate:return "gyro_rate";
    case Rejection::AccelerationMotion:return "accel_motion";
    case Rejection::GyroVariation:return "gyro_variation";
    default:return "none";
  }
}
// Separate from the long performance line so a narrow serial monitor can show
// the entire calibration diagnosis. Never send a truncated line.
inline int formatImuDiagnostic(char* output,size_t size,const Snapshot& state,
                               uint32_t now,float sampleHz){
  const auto a=state.rawAcceleration;
  const float gravity=std::sqrt(a.x*a.x+a.y*a.y+a.z*a.z);
  const int n=std::snprintf(output,size,
    "IMUDIAG v=caldiag3 fw=%s present=%d fault=%d valid=%d cal=%u/400 peak=%u reject=%lu reason=%s gyro_dps=%.3f,%.3f,%.3f g=%.3f hz=%.1f invalid=%lu gaps=%lu age_ms=%lu\n",
    FirmwareVersion,state.present,state.sampleFault,currentAttitude(state,now),state.calibrationCount,state.calibrationPeak,
    static_cast<unsigned long>(state.calibrationRejects),calibrationReasonName(state.calibrationReason),
    state.gyroDps.x,state.gyroDps.y,state.gyroDps.z,gravity,sampleHz,
    static_cast<unsigned long>(state.imuInvalidSamples),static_cast<unsigned long>(state.imuSampleGaps),
    static_cast<unsigned long>(state.samples?uint32_t(now-state.sampledAt):0));
  return n>0&&n<static_cast<int>(size)?n:0;
}
}
