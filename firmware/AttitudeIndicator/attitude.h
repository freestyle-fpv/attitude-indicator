#pragma once
#include <cstdint>
namespace instrument {
struct Vec3 { float x, y, z; };
class AttitudeFilter {
public:
  void reset(Vec3 acceleration);
  bool restoreReference(Vec3 zeroAcceleration,Vec3 currentAcceleration);
  bool update(Vec3 acceleration, Vec3 gyroRadPerSec, float dtSeconds);
  void zeroReference();
  float pitch() const;
  float roll() const;
private:
  float q_[4] = {1,0,0,0};
  float ref_[4] = {1,0,0,0};
  void relative(float out[4]) const;
};
class StationaryCalibration {
public:
  enum class Rejection : uint8_t { None, Nonfinite, GravityRange, GyroRate,
                                  AccelerationMotion, GyroVariation };
  void reset();
  bool add(Vec3 acceleration, Vec3 gyroRadPerSec);
  Rejection rejection() const { return rejection_; }
  unsigned count() const { return count_; }
  Vec3 bias() const { return gyroMean_; }
  Vec3 acceleration() const { return accelMean_; }
  static constexpr unsigned RequiredSamples = 400;
  static constexpr float MaxRawRateDps = 10.f;
  static constexpr float MaxRateChangeDps = 5.f;
  static constexpr float MaxInstantAccelerationChangeG = .075f;
  static constexpr float MaxMeanAccelerationChangeG = .05f;
private:
  Rejection rejection_ = Rejection::None;
  unsigned count_ = 0;
  unsigned pending_ = 0;
  Vec3 gyroMean_{0,0,0}, accelMean_{0,0,0}, first_{0,0,0};
  Vec3 gyroSum_{0,0,0}, accelSum_{0,0,0};
};
}
