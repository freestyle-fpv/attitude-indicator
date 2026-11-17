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
  for(int i=0;i<4;++i)c.add({0,0,-1},{0,0.049f,0});
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
  std::printf("%d failures\n",failures);
  return failures?1:0;
}
