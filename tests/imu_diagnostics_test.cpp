#include "imu_diagnostics.h"
#include <cstdio>
#include <cstring>
#include <limits>

static int failures=0;
static void check(bool ok,const char* name){
  std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;
}
int main(){
  Snapshot state;
  state.present=true;state.sampledAt=1000;state.samples=200;
  state.rawAcceleration={-.298f,-.375f,.917f};
  state.gyroDps={6.25f,-.5f,.125f};
  state.calibrationReason=instrument::StationaryCalibration::Rejection::GyroRate;
  state.calibrationRejects=200;state.calibrationPeak=4;
  char message[320];
  const int n=instrument::formatImuDiagnostic(message,sizeof(message),state,1005,199.5f);
  check(n>0&&n<int(sizeof(message))&&message[n-1]=='\n',"IMU diagnostic is a complete bounded standalone line");
  check(std::strstr(message,"gyro_dps=6.250,-0.500,0.125")&&
        std::strstr(message,"g=1.035")&&std::strstr(message,"reason=gyro_rate"),
    "diagnostic distinguishes plausible gravity from blocked raw gyro calibration");
  check(std::strstr(message,"cal=0/400")&&std::strstr(message,"peak=4")&&
        std::strstr(message,"reject=200")&&std::strstr(message,"age_ms=5")&&
        std::strstr(message,"hz=199.5"),"diagnostic preserves progress, lost windows and sample freshness");
  state.sampleFault=true;state.imuInvalidSamples=17;state.imuSampleGaps=3;
  instrument::formatImuDiagnostic(message,sizeof(message),state,1005,0);
  check(std::strstr(message,"fault=1")&&std::strstr(message,"invalid=17")&&std::strstr(message,"gaps=3"),
    "diagnostic distinguishes data faults from calibration motion rejection");
  state.calibrationRejects=state.imuInvalidSamples=state.imuSampleGaps=UINT32_MAX;
  state.gyroDps={-1024,1024,-1024};
  check(instrument::formatImuDiagnostic(message,sizeof(message),state,UINT32_MAX,200)>0,
    "large counters and full-range gyro still fit the diagnostic buffer");
  char tiny[8];
  check(instrument::formatImuDiagnostic(tiny,sizeof(tiny),state,0,0)==0,
    "truncated diagnostic is rejected instead of sent as a misleading partial line");
  state.calibrated=true;state.sampleFault=false;state.calibrationCount=400;
  instrument::formatImuDiagnostic(message,sizeof(message),state,1005,200);
  check(std::strstr(message,"valid=1"),"healthy diagnostic explicitly authorizes fresh calibrated attitude");
  instrument::formatImuDiagnostic(message,sizeof(message),state,1200,0);
  check(std::strstr(message,"valid=0"),"diagnostic does not authorize an old calibrated sample");
  std::printf("%d failures\n",failures);return failures?1:0;
}
