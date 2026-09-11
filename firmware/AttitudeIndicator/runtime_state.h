#pragma once
#include "attitude.h"
#include <cstdint>
using instrument::Vec3;
struct Snapshot {
  float pitch=0, roll=0;
  uint32_t sampledAt=0;
  unsigned calibrationCount=0;
  uint32_t samples=0,calibrationId=0;
  bool present=false, calibrated=false, sampleFault=false;
  Vec3 rawAcceleration{0,0,0};
  Vec3 gyroDps{0,0,0};
};
