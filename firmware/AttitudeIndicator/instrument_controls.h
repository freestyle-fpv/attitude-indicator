#pragma once
#include <cstdint>
#include "altimeter_scale.h"
namespace instrument {
class InstrumentControls {
 public:
  enum Action { None,DisplayChanged,Calibrate };
  // Order for upward swipes, starting at the power-on dial.
  enum class Mode { Attitude,Hud,Airspeed,Altimeter };
  bool airspeed() const{return mode_==Mode::Airspeed;}
  bool attitude() const{return mode_==Mode::Attitude;}
  bool altimeter() const{return mode_==Mode::Altimeter;}
  bool hud() const{return mode_==Mode::Hud;}
  bool demo() const{return airspeed()?speedDemo_:altimeter()?altitudeDemo_:hud()?hudDemo_:attitudeDemo_;}
  float speed() const{return shown_;}
  float altitude() const{return altitudeShown_;}
  float climb() const{return climb_;}
  uint32_t demoStarted() const{return airspeed()?speedStarted_:altimeter()?altitudeStarted_:hud()?hudStarted_:attitudeStarted_;}
  Action shortPress();
  Action step(int direction);
  Action toggleDemo(uint32_t now);
  Action input(char c,uint32_t now);
  void update(uint32_t now);
 private:
  Mode mode_=Mode::Attitude;
  bool speedDemo_=true,attitudeDemo_=false,haveUpdate_=false;
  bool altitudeDemo_=true,readingAltitude_=false;
  bool hudDemo_=false;
  bool readingSpeed_=false,overflow_=false;
  unsigned used_=0;
  char buffer_[24]={};
  float target_=135,shown_=135,demoOrigin_=135;
  float altitudeTarget_=altitude_scale::DefaultM,altitudeShown_=altitude_scale::DefaultM,altitudeOrigin_=altitude_scale::DefaultM;
  float climb_=0;
  uint32_t hudStarted_=0;
  uint32_t altitudeStarted_=0;
  uint32_t speedStarted_=0,attitudeStarted_=0,updated_=0;
};
}
