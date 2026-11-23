#include "calibration_jitter_fixture.h"
#include "../firmware/AttitudeIndicator/attitude.h"
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>
using namespace instrument;
static int failures=0;
static void check(bool ok,const char* name) { std::printf("%s %s\n",ok?"PASS":"FAIL",name); if(!ok)++failures; }
static bool near(float a,float b,float e=0.3f){return std::fabs(a-b)<e;}
int main(){
  AttitudeFilter f;
  f.reset({0,0,-1});
  check(near(f.pitch(),0)&&near(f.roll(),0),"level initialization");
  f.reset({0.5f,0,-0.8660254f});
  check(near(f.pitch(),30),"nose up from gravity has positive pitch");
  f.reset({0,-0.70710678f,-0.70710678f});
  check(near(f.roll(),45),"right wing down from gravity has positive roll");
  f.zeroReference();
  check(near(f.roll(),0)&&near(f.pitch(),0),"reference trim removes mounting tilt");
  f.reset({0,-0.342020143f,-0.939692621f});
  f.zeroReference();
  for(int i=0;i<200;++i)f.update({0,-0.342020143f,-0.939692621f},{0,0.537243985f,1.476065717f},0.005f);
  check(near(f.pitch(),0)&&near(f.roll(),0),"yaw with 20-degree mounting offset cannot create false tilt");
  f.reset({0,0,-1});
  bool accepted=true;
  for(int i=0;i<200;++i)accepted &= f.update({0,0,0},{1.570796327f,0,0},0.005f);
  check(accepted&&near(f.roll(),90),"one second of 90 deg/s yields 90 degree roll without acceleration");
  for(int i=0;i<220;++i)f.update({0,0,0},{1.570796327f,0,0},0.005f);
  check(near(f.roll(),-171),"roll crosses inversion continuously in quaternion state");
  float before=f.roll();
  check(!f.update({0,0,-1},{1,0,0},0)&&!f.update({0,0,-1},{1,0,0},0.2f)&&near(before,f.roll()),"invalid time steps preserve orientation");
  check(!f.update({NAN,0,-1},{0,0,0},0.005f)&&!f.update({0,0,-1},{INFINITY,0,0},0.005f)&&near(before,f.roll()),"nonfinite samples cannot corrupt quaternion");
  f.reset({0,0,-1});
  for(int i=0;i<600;++i)f.update({0,-0.5f,-0.8660254f},{0,0,0},0.005f);
  check(near(f.roll(),30,0.8f),"gravity correction converges in correct direction");
  f.reset({0,0,-1});
  for(int i=0;i<200;++i)f.update({0,-2,0},{0,0,0},0.005f);
  check(near(f.roll(),0),"2g acceleration is excluded from gravity correction");
  f.reset({0,0,-1});
  for(int i=0;i<20000;++i)f.update({0,0,0},{0.3f,0.2f,0.4f},0.005f);
  check(std::isfinite(f.roll())&&std::isfinite(f.pitch()),"long gyro sequence stays normalized and finite");
  StationaryCalibration c;
  bool ready=false;
  for(unsigned i=0;i<399;++i)ready |= c.add({0,0,-1},{0.005f,-0.002f,0.001f});
  check(!ready,"calibration requires complete stationary sample window");
  check(c.add({0,0,-1},{0.005f,-0.002f,0.001f})&&near(c.bias().x,0.005f,0.00001f),"calibration estimates stationary gyro bias");
  c.reset();
  for(unsigned i=0;i<400;++i)ready=c.add({0,0,-1},{0.004f,0.026f,0.006f});
  check(ready&&near(c.bias().y,0.026f,0.00001f),"measured 1.55 deg/s stationary QMI bias can be calibrated");
  const float radiansPerDegree=0.01745329252f;
  const Vec3 reportedBias={-.21f*radiansPerDegree,3.45f*radiansPerDegree,-4.01f*radiansPerDegree};
  c.reset();ready=false;
  for(unsigned i=0;i<399;++i){
    const float noise=(i%2?.12f:-.12f)*radiansPerDegree;
    ready|=c.add({.985f,.1809f,-.0311f},{reportedBias.x+noise,reportedBias.y-noise,reportedBias.z+noise});
  }
  check(!ready&&c.count()<400,"reported 5.29 deg/s bias still requires a complete stationary window");
  const float noise=.12f*radiansPerDegree;
  ready=c.add({.985f,.1809f,-.0311f},{reportedBias.x+noise,reportedBias.y-noise,reportedBias.z+noise});
  check(ready&&near(c.bias().x,reportedBias.x,.000001f)&&near(c.bias().y,reportedBias.y,.000001f)&&
    near(c.bias().z,reportedBias.z,.000001f),"reported 5.29 deg/s stable bias calibrates despite small sample noise");
  c.reset();
  for(unsigned i=0;i<400;++i)ready=c.add({0,0,-1},{9.99f*radiansPerDegree,0,0});
  check(ready,"stationary bias just below 10 deg/s is accepted");
  c.add({0,0,-1},{10.01f*radiansPerDegree,0,0});
  check(c.count()==0&&c.rejection()==StationaryCalibration::Rejection::GyroRate,
    "sample just above 10 deg/s resets calibration with raw-rate rejection");
  c.add({0,0,-1},{6*radiansPerDegree,6*radiansPerDegree,6*radiansPerDegree});
  check(c.count()==0&&c.rejection()==StationaryCalibration::Rejection::GyroRate,
    "10 deg/s ceiling applies to the combined vector rather than each axis");
  // The vertical bench fixture measured about 0.91g on this board.
  for(const Vec3 axis:{Vec3{.910f,0,-.025f},Vec3{-.910f,0,.025f},Vec3{0,.910f,-.025f},Vec3{0,-.910f,.025f}}){
    c.reset();
    for(unsigned i=0;i<400;++i)ready=c.add(axis,{.004f,.026f,.006f});
    check(ready,"vertical stationary calibration accepts the measured 0.91g scale");
  }
  c.reset();c.add({0,0,.8f},{0,0,0});
  check(c.count()==0,"calibration rejects acceleration below the gravity window");
  for(unsigned i=0;i<400;++i)ready=c.add({0,0,-.91f},{0,.026f+(i%2?.018f:-.018f),0});
  check(ready&&near(c.bias().y,.026f,.0001f),"short gyro fluctuations average out on a stationary fixture");
  c.reset();
  for(unsigned i=0;i<100;++i)c.add({0,0,-1},{0,0.026f,0});
  for(int i=0;i<4;++i)c.add({0,0,-1},{0,0.146f,0});
  check(c.count()==0,"changing angular velocity rejects motion below absolute bias ceiling");
  c.reset();
  for(int i=0;i<200;++i)c.add({0,0,-1},{0,0,0});
  c.add({0,0,-1},{0.2f,0,0});
  check(c.count()==0,"rotation restarts stationary window");
  for(int i=0;i<200;++i)c.add({0,0,-1},{0,0,0});
  c.add({0.1f,0,-0.995f},{0,0,0});
  check(c.count()==0,"changing gravity direction restarts stationary window");
  c.add({NAN,0,0},{0,0,0});
  check(c.count()==0,"calibration rejects nonfinite acceleration");
  check(c.rejection()==StationaryCalibration::Rejection::Nonfinite,
    "calibration diagnostics identify nonfinite samples");
  c.add({-.298f,-.375f,.917f},{.2f,0,0});
  check(c.count()==0&&c.rejection()==StationaryCalibration::Rejection::GyroRate,
    "normal-gravity startup blockage identifies excessive raw gyro rate");
  c.add({0,0,.8f},{0,0,0});
  check(c.rejection()==StationaryCalibration::Rejection::GravityRange,
    "calibration diagnostics distinguish invalid gravity from gyro motion");
  c.reset();
  for(unsigned i=0;i<100;++i)c.add({0,0,-1},{0,.026f,0});
  for(unsigned i=0;i<4;++i)c.add({0,0,-1},{0,.146f,0});
  check(c.count()==0&&c.rejection()==StationaryCalibration::Rejection::GyroVariation,
    "calibration diagnostics identify a changing gyro block mean");
  c.reset();
  for(unsigned i=0;i<100;++i)c.add({0,0,-1},{0,0,0});
  for(unsigned i=0;i<4;++i)c.add({.06f,0,-1},{0,0,0});
  check(c.count()==0&&c.rejection()==StationaryCalibration::Rejection::AccelerationMotion,
    "calibration diagnostics identify gravity-direction changes below the instant-motion gate");
  c.reset();
  for(unsigned i=0;i<400;++i)ready=c.add({-.298f,-.375f,.917f},{.004f,.026f,.006f});
  check(ready&&c.rejection()==StationaryCalibration::Rejection::None,
    "reported 1.03g gravity vector completes calibration when the gyro is stable");
  for(unsigned mode=0;mode<3;++mode){
    c.reset();ready=false;
    for(unsigned i=0;i<399;++i){auto sample=calibration_fixture::mild(i);
      if(mode==0)sample.acceleration={0,0,-1};
      if(mode==1)sample.gyro=calibration_fixture::bias();
      ready|=c.add(sample.acceleration,sample.gyro);
    }
    check(!ready&&c.count()<400,"mild jitter still cannot finish calibration before 400 samples");
    auto sample=calibration_fixture::mild(399);
    if(mode==0)sample.acceleration={0,0,-1};
    if(mode==1)sample.gyro=calibration_fixture::bias();
    ready=c.add(sample.acceleration,sample.gyro);
    check(ready&&c.count()==400,mode==0?"bounded gyro vibration can complete calibration":mode==1?"bounded acceleration vibration can complete calibration":"combined mild vibration can complete calibration");
    const auto expected=calibration_fixture::bias(),measured=c.bias(),gravity=c.acceleration();
    check(ready&&near(measured.x,expected.x,.000001f)&&near(measured.y,expected.y,.000001f)&&near(measured.z,expected.z,.000001f),
      "balanced mild jitter does not bias the estimated stationary gyro offset");
    check(ready&&near(gravity.x,0,.00001f)&&near(gravity.y,0,.00001f)&&near(gravity.z,-1,.00001f),
      "balanced acceleration jitter preserves the mean mounting reference");
  }
  c.reset();ready=false;
  for(unsigned i=0;i<400;++i){
    const float angle=4.f*radiansPerDegree*i/399;
    ready|=c.add({std::sin(angle),0,-std::cos(angle)},{0,2*radiansPerDegree,0});
  }
  check(!ready&&c.count()<400,"continuous four-degree pitch change cannot masquerade as a full stable window");
  for(const float change:{4.99f,5.01f}){
    c.reset();for(unsigned i=0;i<100;++i)c.add({0,0,-1},{0,0,0});
    for(unsigned i=0;i<4;++i)c.add({0,0,-1},{0,change*radiansPerDegree,0});
    check(change<5?c.count()==104:c.count()==0&&c.rejection()==StationaryCalibration::Rejection::GyroVariation,
      "five-degree-per-second gyro-variation gate accepts below and rejects above its boundary");
  }
  for(const float change:{.049f,.051f}){
    c.reset();for(unsigned i=0;i<100;++i)c.add({0,0,-1},{0,0,0});
    for(unsigned i=0;i<4;++i)c.add({change,0,-1},{0,0,0});
    check(change<.05f?c.count()==104:c.count()==0&&c.rejection()==StationaryCalibration::Rejection::AccelerationMotion,
      "mean acceleration gate accepts mild displacement and rejects above 0.05g");
  }
  c.reset();for(unsigned i=0;i<100;++i)c.add({0,0,-1},{0,0,0});
  c.add({.076f,0,-1},{0,0,0});
  check(c.count()==0&&c.rejection()==StationaryCalibration::Rejection::AccelerationMotion,
    "instant acceleration spike still resets the complete calibration window");
  std::printf("%d failures\n",failures);
  return failures?1:0;
}
