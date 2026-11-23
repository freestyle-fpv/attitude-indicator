#include "attitude.h"
#include <cmath>
namespace instrument {
namespace {
constexpr float Degrees = 57.295779513f;
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
float norm(Vec3 v) { return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z); }
float clampUnit(float x) { return x < -1 ? -1 : (x > 1 ? 1 : x); }
void multiply(const float* a,const float* b,float* o) {
  o[0]=a[0]*b[0]-a[1]*b[1]-a[2]*b[2]-a[3]*b[3];
  o[1]=a[0]*b[1]+a[1]*b[0]+a[2]*b[3]-a[3]*b[2];
  o[2]=a[0]*b[2]-a[1]*b[3]+a[2]*b[0]+a[3]*b[1];
  o[3]=a[0]*b[3]+a[1]*b[2]-a[2]*b[1]+a[3]*b[0];
}
}
// Body coordinates: X forward, Y right, Z down (right handed).
// An accelerometer measures specific force: at level rest, a = (0,0,-1) g.
void AttitudeFilter::reset(Vec3 a) {
  float n=norm(a);
  if(!finite(a)||!std::isfinite(n)||n<0.1f) return;
  float p=std::asin(clampUnit(a.x/n));
  float r=std::atan2(-a.y,-a.z);
  float cp=std::cos(p/2),sp=std::sin(p/2),cr=std::cos(r/2),sr=std::sin(r/2);
  q_[0]=cr*cp; q_[1]=sr*cp; q_[2]=cr*sp; q_[3]=-sr*sp;
  ref_[0]=1; ref_[1]=ref_[2]=ref_[3]=0;
}
bool AttitudeFilter::update(Vec3 a,Vec3 w,float dt) {
  if(!finite(a)||!finite(w)||!std::isfinite(dt)||dt<=0||dt>0.1f) return false;
  float n=norm(a);
  if(!std::isfinite(n)||norm(w)>40.0f) return false;
  // Accelerometer correction is gated under large linear acceleration.
  // This cannot distinguish sustained coordinated flight acceleration from gravity.
  if(n>0.85f&&n<1.15f) {
    Vec3 g{-a.x/n,-a.y/n,-a.z/n};
    Vec3 estimate{2*(q_[1]*q_[3]-q_[0]*q_[2]),
                  2*(q_[2]*q_[3]+q_[0]*q_[1]),
                  1-2*(q_[1]*q_[1]+q_[2]*q_[2])};
    constexpr float Kp=1.5f;
    w.x+=Kp*(g.y*estimate.z-g.z*estimate.y);
    w.y+=Kp*(g.z*estimate.x-g.x*estimate.z);
    w.z+=Kp*(g.x*estimate.y-g.y*estimate.x);
  }
  const float spin[4]={0,w.x,w.y,w.z};
  float derivative[4],next[4],sum=0;
  multiply(q_,spin,derivative);
  for(int i=0;i<4;++i){next[i]=q_[i]+0.5f*dt*derivative[i];sum+=next[i]*next[i];}
  if(!std::isfinite(sum)||sum<0.1f) return false;
  const float inverse=1/std::sqrt(sum);
  for(int i=0;i<4;++i)q_[i]=next[i]*inverse;
  return true;
}
void AttitudeFilter::zeroReference() { for(int i=0;i<4;++i)ref_[i]=q_[i]; }
void AttitudeFilter::relative(float* out) const {
  float conjugate[4]={ref_[0],-ref_[1],-ref_[2],-ref_[3]};
  // q = aircraft orientation * fixed mounting rotation. Remove the mounting
  // rotation on the right, so a pure heading change cannot become false tilt.
  multiply(q_,conjugate,out);
}
float AttitudeFilter::pitch() const {
  float q[4]; relative(q);
  return Degrees*std::asin(clampUnit(2*(q[0]*q[2]-q[3]*q[1])));
}
float AttitudeFilter::roll() const {
  float q[4]; relative(q);
  return Degrees*std::atan2(2*(q[0]*q[1]+q[2]*q[3]),1-2*(q[1]*q[1]+q[2]*q[2]));
}
void StationaryCalibration::reset() {
  rejection_=Rejection::None;
  count_=pending_=0;gyroMean_={0,0,0};accelMean_={0,0,0};first_={0,0,0};
  gyroSum_=accelSum_={0,0,0};
}
bool StationaryCalibration::add(Vec3 a,Vec3 w) {
  rejection_=Rejection::None;
  const auto reject=[this](Rejection reason){reset();rejection_=reason;return false;};
  const float n=norm(a);
  // Before calibration, constant gyro bias is not evidence of motion. This
  // board measured ~1.5 deg/s at rest. Bound the plausible raw bias separately
  // from variation around its running mean (all rates below are radians/s).
  constexpr float MaxRawRate=0.17453293f; // 10 deg/s
  constexpr float MaxRateChange=0.01308997f; // 0.75 deg/s
  // Use the same gravity gate as fusion. This board reads about 0.91 g when
  // supported on some edges; the separate variation checks still reject motion.
  if(!finite(a)||!finite(w))return reject(Rejection::Nonfinite);
  if(n<=0.85f||n>=1.15f)return reject(Rejection::GravityRange);
  if(norm(w)>MaxRawRate)return reject(Rejection::GyroRate);
  if(count_>=RequiredSamples)return true;
  if(count_>0){
    const Vec3 delta{a.x-first_.x,a.y-first_.y,a.z-first_.z};
    if(norm(delta)>.075f)return reject(Rejection::AccelerationMotion);
  }
  // At 200 Hz a block spans 20 ms. Compare its mean with the stationary
  // reference so isolated sensor jitter cannot restart a two-second wait.
  // Instantaneous saturation, gravity and large motion checks still apply.
  constexpr unsigned Block=4;
  static_assert(RequiredSamples%Block==0,"calibration must use full blocks");
  gyroSum_={gyroSum_.x+w.x,gyroSum_.y+w.y,gyroSum_.z+w.z};
  accelSum_={accelSum_.x+a.x,accelSum_.y+a.y,accelSum_.z+a.z};
  if(++pending_<Block)return false;
  w={gyroSum_.x/Block,gyroSum_.y/Block,gyroSum_.z/Block};
  a={accelSum_.x/Block,accelSum_.y/Block,accelSum_.z/Block};
  gyroSum_=accelSum_={0,0,0};pending_=0;
  if(count_>0){
    Vec3 rateChange{w.x-gyroMean_.x,w.y-gyroMean_.y,w.z-gyroMean_.z};
    if(norm(rateChange)>MaxRateChange)return reject(Rejection::GyroVariation);
  }
  if(count_==0)first_=a;
  Vec3 delta{a.x-first_.x,a.y-first_.y,a.z-first_.z};
  if(norm(delta)>0.025f)return reject(Rejection::AccelerationMotion);
  count_+=Block;
  const float weight=float(Block)/count_;
  gyroMean_.x+=(w.x-gyroMean_.x)*weight;
  gyroMean_.y+=(w.y-gyroMean_.y)*weight;
  gyroMean_.z+=(w.z-gyroMean_.z)*weight;
  accelMean_.x+=(a.x-accelMean_.x)*weight;
  accelMean_.y+=(a.y-accelMean_.y)*weight;
  accelMean_.z+=(a.z-accelMean_.z)*weight;
  return count_>=RequiredSamples;
}
}
