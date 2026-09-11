#include "instrument_controls.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <limits>
namespace instrument {
InstrumentControls::Action InstrumentControls::step(int direction){if(!direction)return None;mode_=static_cast<Mode>((static_cast<unsigned>(mode_)+(direction>0?1:3))%4);return DisplayChanged;}
InstrumentControls::Action InstrumentControls::shortPress(){attitudeDemo_=false;hudDemo_=false;return Calibrate;}
InstrumentControls::Action InstrumentControls::toggleDemo(uint32_t now){
  update(now);
  if(airspeed()){speedDemo_=!speedDemo_;speedStarted_=now;demoOrigin_=shown_;target_=shown_;}
  else if(altimeter()){altitudeDemo_=!altitudeDemo_;altitudeStarted_=now;altitudeOrigin_=altitudeShown_;altitudeTarget_=altitudeShown_;}
  else if(hud()){hudDemo_=!hudDemo_;hudStarted_=now;}
  else {attitudeDemo_=!attitudeDemo_;attitudeStarted_=now;}
  return DisplayChanged;
}
void InstrumentControls::update(uint32_t now){
  const float elapsed=haveUpdate_?static_cast<uint32_t>(now-updated_)*.001f:0;
  const float dt=std::min(.1f,elapsed),previousAltitude=altitudeShown_;
  updated_=now;haveUpdate_=true;
  if(speedDemo_){
    float t=static_cast<uint32_t>(now-speedStarted_)*.001f,blend=1-std::exp(-t*.7f);
    target_=demoOrigin_*(1-blend)+(100+100*std::sin(t*.55f))*blend;
  }
  if(altitudeDemo_){
    const float t=static_cast<uint32_t>(now-altitudeStarted_)*.001f,blend=1-std::exp(-t*.7f);
    altitudeTarget_=altitudeOrigin_*(1-blend)+(altitude_scale::DefaultM+altitude_scale::DemoAmplitudeM*std::sin(t*.15f))*blend;
  }
  altitudeShown_+=(altitudeTarget_-altitudeShown_)*(1-std::exp(-dt*14));
  if(std::fabs(altitudeTarget_-altitudeShown_)<=std::max(.01f,2*std::numeric_limits<float>::epsilon()*std::fabs(altitudeTarget_)))altitudeShown_=altitudeTarget_;
  altitudeShown_=std::max(0.f,std::min(altitude_scale::MaxM,altitudeShown_));
  // Derive metres per second from the smoothed altitude and real elapsed time.
  // Repeated timestamps keep the last rate instead of dividing by zero.
  if(elapsed>0)climb_=std::max(-15.f,std::min(15.f,(altitudeShown_-previousAltitude)/elapsed));
  shown_+=(target_-shown_)*(1-std::exp(-dt*14));
  if(std::fabs(target_-shown_)<.01f)shown_=target_;
  shown_=std::max(0.f,std::min(200.f,shown_));
}
InstrumentControls::Action InstrumentControls::input(char c,uint32_t now){
  if(readingSpeed_||readingAltitude_){
    if(c=='\n'||c=='\r'){
      const bool altitudeInput=readingAltitude_;
      readingSpeed_=readingAltitude_=false;buffer_[used_]=0;
      if(overflow_||!used_)return None;
      char* end=nullptr;errno=0;float value=std::strtof(buffer_,&end);
      if(errno||end==buffer_||*end||!std::isfinite(value))return None;
      update(now);
      if(altitudeInput){altitudeTarget_=std::max(0.f,std::min(altitude_scale::MaxM,value));altitudeDemo_=false;mode_=Mode::Altimeter;}
      else {target_=std::max(0.f,std::min(200.f,value));speedDemo_=false;mode_=Mode::Airspeed;}
      return DisplayChanged;
    }
    if(used_<sizeof(buffer_)-1)buffer_[used_++]=c;else overflow_=true;
    return None;
  }
  if(c=='v'||c=='b'){readingSpeed_=c=='v';readingAltitude_=c=='b';used_=0;overflow_=false;return None;}
  if(c=='a'&&!airspeed()){mode_=Mode::Airspeed;return DisplayChanged;}
  if(c=='h'&&!attitude()){mode_=Mode::Attitude;return DisplayChanged;}
  if(c=='t'&&!altimeter()){mode_=Mode::Altimeter;return DisplayChanged;}
  if(c=='u'&&!hud()){mode_=Mode::Hud;return DisplayChanged;}
  if(c=='d')return toggleDemo(now);
  if(c=='c')return shortPress();
  return None;
}
}
