#pragma once
#include "attitude.h"
#include <cstddef>
#include <cstdint>
#include <cmath>
namespace instrument {
struct ImuSelfTestResult {int8_t code=-1;Vec3 response{0,0,0};bool restored=false;};
// QST QMI8658A Rev A, section 15: checked register access and bounded waits.
// https://files.waveshare.com/wiki/common/QMI8658A_Datasheet_Rev_A.pdf
// Codes: -1 untested, 0 response below threshold, 1 pass, 3 bus error, 4 timeout.
template<class Bus> ImuSelfTestResult imuSelfTest(Bus& bus,bool gyro){
 ImuSelfTestResult result;uint8_t saved[3];
 if(!bus.read(3,&saved[0],1)||!bus.read(4,&saved[1],1)||!bus.read(0x08,&saved[2],1)){result.code=3;return result;}
 const uint8_t control=gyro?4:3,value=gyro?(saved[1]|0x80):((saved[0]&0x70)|0x83);
 auto waitStatus=[&](bool high){for(unsigned i=0;i<200;++i){uint8_t status=0;if(!bus.read(0x2d,&status,1))return int8_t(3);if(bool(status&1)==high)return int8_t(1);bus.wait();}return int8_t(4);};
 result.code=3;
 if(bus.write(0x08,0)&&bus.write(control,value)){
  result.code=waitStatus(true);
  if(result.code==1){
   if(!bus.write(control,value&0x7f))result.code=3;
   else result.code=waitStatus(false);
   if(result.code==1){
    uint8_t raw[6];
    if(!bus.read(0x51,raw,6))result.code=3;
    else{
     float value[3];for(unsigned i=0;i<3;++i)value[i]=int16_t(uint16_t(raw[i*2])|(uint16_t(raw[i*2+1])<<8))*(gyro?0.0625f:0.5f);
     result.response={value[0],value[1],value[2]};
     const float threshold=gyro?300.f:200.f;
     result.code=(std::fabs(value[0])>threshold&&std::fabs(value[1])>threshold&&std::fabs(value[2])>threshold)?1:0;
    }
   }
  }
 }
 // Try every restore write even if another write failed. No persistent offsets.
 const bool a=bus.write(3,saved[0]&0x7f),g=bus.write(4,saved[1]&0x7f),enable=bus.write(0x08,saved[2]);
 result.restored=a&&g&&enable;return result;
}
inline bool testMemoryBlock(volatile uint32_t* memory,size_t words){
 if(!memory||!words)return false;
 for(unsigned pass=0;pass<4;++pass){
  for(size_t i=0;i<words;++i)memory[i]=pass==0?0:pass==1?UINT32_MAX:pass==2?0xAA55AA55u:uint32_t(i)^0x5AA55AA5u;
  for(size_t i=0;i<words;++i)if(memory[i]!=(pass==0?0:pass==1?UINT32_MAX:pass==2?0xAA55AA55u:uint32_t(i)^0x5AA55AA5u))return false;
 }return true;
}
class DiagnosticCadence{
 public:bool due(uint32_t now){
  if(!started_){started_=true;last_=now;return false;}
  const uint32_t elapsed=now-last_;if(elapsed<50)return false;
  last_=elapsed>=150?now:last_+50;return true;
 }
 private:uint32_t last_=0;bool started_=false;
};
class DiagnosticLease{
 public:void renew(uint32_t now){at_=now;enabled_=true;}void stop(){enabled_=false;}
 bool active(uint32_t now)const{return enabled_&&uint32_t(now-at_)<15000;}
 private:uint32_t at_=0;bool enabled_=false;
};
struct HardwareState{
 int16_t altWho=-1,who=-1,revision=-1,ctrl2=-1,ctrl3=-1,ctrl7=-1;
 float temperature=NAN;uint32_t nack=0,shortRead=0,saturated=0,notReady=0,repeatedTimestamp=0,timestamp=0;
 int8_t rtc=-1,rtcSec=-1,rtcOs=-1,rtcStop=-1,rtcTick=-1,dac=-1,microphone=-1;
 int16_t adc=-1;int8_t pmu=-1,touch=-1,expander=-1,battery=-1,vbusGood=-1;uint16_t vbusMv=0,systemMv=0,batteryMv=0;
 bool button=false,bootDown=false,pwrDown=false,testing=false,restored=true;
 uint32_t selfTestId=0,bootClicks=0;ImuSelfTestResult accelSelf,gyroSelf;
};
}
