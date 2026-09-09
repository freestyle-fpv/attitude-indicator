#include "board_config.h"
#include <cmath>
#include <cstdio>
using instrument::Vec3;
bool close(float x,float y){return std::fabs(x-y)<.01f;}
int main(){
  auto level=board::toBody({1,0,0});
  if(!close(level.x,0)||!close(level.y,0)||!close(level.z,-1))return 1;
  instrument::AttitudeFilter filter;
  filter.reset(board::toBody({.8660254f,0,.5f}));
  if(!close(filter.pitch(),30))return 2;
  filter.reset(board::toBody({.8660254f,-.5f,0}));
  if(!close(filter.roll(),30))return 3;
  filter.reset(board::toBody({0,0,-1}));
  if(!close(filter.pitch(),-90))return 5;
  Vec3 x=board::toBody({1,0,0}),y=board::toBody({0,1,0}),z=board::toBody({0,0,1});
  if(!close(x.y*y.z-x.z*y.y,z.x)||!close(x.z*y.x-x.x*y.z,z.y)||!close(x.x*y.y-x.y*y.x,z.z))return 4;
  std::puts("PASS USB-left sensor mapping: level, pitch, roll and right-handed rotation");
}
