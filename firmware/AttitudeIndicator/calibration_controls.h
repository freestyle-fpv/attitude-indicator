#pragma once
#include <cstdint>
namespace instrument {
class CalibrationButton {
 public:
  bool update(bool valid,bool pressed,uint32_t now){
    if(!valid){initialized_=false;armed_=false;return false;}
    if(!initialized_){initialized_=true;raw_=stable_=pressed;changed_=now;armed_=false;return false;}
    if(pressed!=raw_){raw_=pressed;changed_=now;}
    if(raw_==stable_||uint32_t(now-changed_)<30)return false;
    stable_=raw_;
    if(stable_){pressedAt_=changed_;armed_=true;return false;}
    const uint32_t held=changed_-pressedAt_;const bool click=armed_&&held>=30&&held<800;
    armed_=false;return click;
  }
 private:
  bool initialized_=false,raw_=false,stable_=false,armed_=false;
  uint32_t changed_=0,pressedAt_=0;
};
// One acknowledged sensor calibration shared by every display. A prior valid
// snapshot cannot prematurely end the modal while the sampler receives it.
class CalibrationSession {
 public:
  uint32_t request(bool restart=false){if(active_&&!restart)return 0;if(++id_==0)++id_;active_=true;return id_;}
  bool observe(uint32_t id,bool calibrated){if(!active_||id!=id_||!calibrated)return false;active_=false;return true;}
  bool active()const{return active_;}
 private:
  uint32_t id_=0;bool active_=false;
};
}
