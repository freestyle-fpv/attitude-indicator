#include "board_config.h"
#include "calibration_controls.h"
#include "display_policy.h"
#include "runtime_state.h"
#include <atomic>
#include <cmath>
#include <cstdio>
#include <vector>

using instrument::Vec3;
using instrument::UsbSide;
using board::SampleResult;
using TickType_t=uint32_t;
constexpr int pdTRUE=1,LOW=0;
constexpr unsigned commands=1,snapshots=2;
constexpr TickType_t pdMS_TO_TICKS(unsigned ms){return ms;}
struct CalibrationRequest {uint32_t id;UsbSide side;};
struct Step {
  uint32_t ms;
  SampleResult result;
  Vec3 acceleration,gyro;
  uint32_t calibrationId=0;
  UsbSide side=UsbSide::Left;
  bool initOk=true,selfTest=false;
  Step(uint32_t t,SampleResult r,Vec3 a={0,0,-1},Vec3 w={0,0,0}):ms(t),result(r),acceleration(a),gyro(w){}
};
static std::vector<Step> steps;
static std::vector<Snapshot> observed;
static size_t cursor=0;
static int failures=0;
static UsbSide initialSide=UsbSide::Left;
static std::atomic<bool> menuClick{false},imuSelfTestRequested{false};
static std::atomic<uint32_t> bootClicks{0};
static instrument::HardwareState imuHardware;
// This harness replaces hardware I/O only; self-test protocol has its own bus tests.
struct ImuTestBus{
 bool read(uint8_t,uint8_t*,size_t){return false;}
 bool write(uint8_t,uint8_t){return false;}
 void wait(){}
};
void pollHardware(uint32_t){}

struct Finished {};
uint32_t millis(){return steps[cursor].ms;}
uint32_t micros(){return millis()*1000u;}
TickType_t xTaskGetTickCount(){return millis();}
int digitalRead(int){return 1;}
void pollTouch(uint32_t){}
void pollPowerButton(uint32_t){}
bool initializeImu(){return steps[cursor].initOk;}
SampleResult readSample(Vec3& a,Vec3& w,Vec3& raw,UsbSide){
  const auto& step=steps[cursor];a=raw=step.acceleration;w=step.gyro;return step.result;
}
int xQueueReceive(unsigned queue,CalibrationRequest* command,unsigned){
  if(steps[cursor].selfTest)imuSelfTestRequested.store(true);
  if(queue!=commands||!steps[cursor].calibrationId)return 0;
  *command={steps[cursor].calibrationId,steps[cursor].side};return pdTRUE;
}
void xQueueOverwrite(unsigned,const Snapshot* state){observed.push_back(*state);}
void vTaskDelayUntil(TickType_t*,TickType_t){if(++cursor==steps.size())throw Finished{};}

// Generated from the actual sketch by imu_recovery_test.sh. Only hardware I/O
// and task scheduling above are replaced; the sampler and fusion are real.
#include "imu_sampling_task.inc"

static void check(bool ok,const char* name){
  std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;
}
static bool near(float a,float b,float tolerance=.3f){return std::fabs(a-b)<tolerance;}
static void run(){
  cursor=0;observed.clear();imuHardware=instrument::HardwareState();imuSelfTestRequested.store(false);bootClicks.store(0);
  try{samplingTask(nullptr);}catch(const Finished&){}
}
static void calibrated(uint32_t start=1000){
  steps.clear();
  // 20-degree installation roll and nonzero gyro bias must survive recovery.
  for(unsigned i=0;i<400;++i)steps.emplace_back(start+i*5,SampleResult::Valid,
    Vec3{0,-.342020143f,-.939692621f},Vec3{.02f,0,0});
  // Move a further 30 degrees with acceleration correction excluded.
  for(unsigned i=0;i<200;++i)steps.emplace_back(start+2000+i*5,SampleResult::Valid,
    Vec3{0,0,0},Vec3{.543598776f,0,0});
}
static void recovered(uint32_t first){
  steps.emplace_back(first,SampleResult::Valid,Vec3{0,0,0},Vec3{.02f,0,0});
  steps.emplace_back(first+5,SampleResult::Valid,Vec3{0,0,0},Vec3{.543598776f,0,0});
}
int main(){
  const Vec3 reportedBias={-.0036651914f,.060213859f,-.069987703f};
  steps.clear();
  for(unsigned i=0;i<800;++i)steps.emplace_back(1000+i*5,SampleResult::Valid,
    i<400?Vec3{0,0,-1}:Vec3{0,0,0},reportedBias);
  run();
  check(!observed[398].calibrated&&observed[399].calibrated,
    "sampler calibrates reported 5.29 deg/s bias only after all 400 samples");
  check(instrument::currentAttitude(observed.back(),4995)&&near(observed.back().pitch,0,.02f)&&
    near(observed.back().roll,0,.02f),"sampler subtracts reported bias without relying on gravity to hide drift");

  calibrated();run();
  check(observed.back().calibrated&&near(observed.back().roll,30),"fixture calibrates mounting trim and gyro bias before motion");

  calibrated();steps.emplace_back(4000,SampleResult::Invalid);recovered(4005);run();
  check(observed[600].sampleFault,"invalid I2C/clipped sample immediately marks attitude unavailable");
  check(!instrument::currentAttitude(observed[600],4000)&&
    !instrument::currentAttitude(observed[601],4005),"fault and first recovered sample cannot authorize a displayed pose");
  check(observed.back().calibrated&&!observed.back().sampleFault,"single invalid sample recovers while moving without stationary calibration");
  check(near(observed.back().roll,30.15f,.02f),"recovered gyro sample advances the preserved mounting-relative pose");
  check(near(observed[601].roll,30),"first recovered sample does not integrate across missing motion");
  check(instrument::currentAttitude(observed.back(),4010),"next integrated sample authorizes attitude again");
  check(!instrument::currentAttitude(observed.back(),4111),"stale recovered pose remains unavailable");
  using instrument::FrameDecision;
  check(instrument::decideFrame(false,true,false,instrument::currentAttitude(observed[600],4000),true)==FrameDecision::Status&&
    instrument::decideHudFrame(false,instrument::currentAttitude(observed[600],4000),true,true)==FrameDecision::Discard,
    "queued round and HUD poses are suppressed when the live sampler faults");
  for(unsigned i=0;i<200;++i)steps.emplace_back(4015+i*5,SampleResult::Valid,Vec3{0,0,0},Vec3{.02f,0,0});
  run();
  check(near(observed.back().roll,30.15f,.02f),"saved gyro bias still cancels drift after recovery");
  for(unsigned i=0;i<1200;++i)steps.emplace_back(5015+i*5,SampleResult::Valid,Vec3{0,-1,0},Vec3{.02f,0,0});
  run();
  check(near(observed.back().roll,70,.3f),"gravity feedback corrects missed tilt while preserving the 20-degree mounting reference");

  calibrated();steps.emplace_back(4150,SampleResult::NotReady);recovered(4155);run();
  check(observed[600].sampleFault,"data-ready timeout marks the old pose unavailable");
  check(observed.back().calibrated&&!observed.back().sampleFault&&near(observed.back().roll,30.15f,.02f),"data-ready timeout recovers while rotating");
  check(observed.back().imuSampleGaps==1,"data-ready timeout is counted once after recovery");

  calibrated();recovered(4200);run();
  check(observed.back().calibrated&&!observed.back().sampleFault&&near(observed.back().roll,30.15f,.02f),"scheduler gap skips missing interval without re-zeroing");
  check(observed.back().imuSampleGaps==1,"long integration interval is reported as one sample gap");

  calibrated();
  for(uint32_t ms=4000;ms<=5000;ms+=5){
    steps.emplace_back(ms,SampleResult::Invalid);steps.back().initOk=false;
  }
  recovered(5510);run();
  check(!observed[701].present,"sustained fault still takes sensor offline for initialization retry");
  check(observed.back().present&&observed.back().calibrated&&!observed.back().sampleFault&&near(observed.back().roll,30.15f),"hardware reinitialization preserves the previous calibration");
  check(instrument::currentAttitude(observed.back(),5515),"reinitialized sensor authorizes a fresh pose");

  calibrated();steps.emplace_back(4000,SampleResult::Valid,Vec3{NAN,0,-1});recovered(4005);run();
  check(observed[600].sampleFault&&std::isfinite(observed[600].roll),"rejected fusion input preserves finite pose and marks a fault");
  check(observed.back().calibrated&&!observed.back().sampleFault,"fusion rejection does not discard calibration");

  for(const UsbSide side:{UsbSide::Left,UsbSide::Top}){
    calibrated();steps.emplace_back(4000,SampleResult::Valid,Vec3{0,0,-1},Vec3{.5f,0,0});
    steps.back().calibrationId=7;steps.back().side=side;
    for(unsigned i=0;i<10;++i)steps.emplace_back(4005+i*5,SampleResult::Valid,Vec3{0,0,-1},Vec3{.5f,0,0});
    const size_t movingEnd=steps.size()-1;
    for(unsigned i=0;i<400;++i)steps.emplace_back(4055+i*5,SampleResult::Valid);
    run();
    check(!observed[movingEnd].calibrated&&observed[movingEnd].calibrationId==7,
      "manual calibration or installation change still requires stillness");
    check(observed.back().calibrated&&near(observed.back().roll,0),"explicit calibration establishes a new zero after 400 stationary samples");
  }

  steps.clear();
  for(unsigned i=0;i<200;++i)steps.emplace_back(1000+i*5,SampleResult::Valid);
  steps.emplace_back(2000,SampleResult::Invalid);
  for(unsigned i=0;i<399;++i)steps.emplace_back(2005+i*5,SampleResult::Valid);
  run();check(!observed.back().calibrated,"startup fault restarts the uncompleted stationary window");
  check(observed.back().imuInvalidSamples==1&&observed.back().calibrationPeak==396,
    "startup diagnostics retain invalid sample count and best completed calibration window");
  steps.emplace_back(4000,SampleResult::Valid);run();
  check(observed.back().calibrated,"startup still needs a complete fresh 400-sample calibration");

  calibrated(4294000u);steps.emplace_back(4297000u,SampleResult::Invalid);recovered(4297005u);run();
  check(observed.back().calibrated&&near(observed.back().roll,30.15f),"micros wrap preserves recovery and timing");
  calibrated(4294964291u);steps.emplace_back(4294967291u,SampleResult::Invalid);recovered(0);run();
  check(instrument::currentAttitude(observed.back(),5)&&near(observed.back().roll,30.15f,.02f),
    "sample at micros zero starts a valid interval across millis wrap");
  steps.clear();
  for(unsigned i=0;i<400;++i)steps.emplace_back(1000+i*5,SampleResult::Valid);
  steps.emplace_back(3000,SampleResult::Invalid);
  steps.back().calibrationId=9;
  for(unsigned i=0;i<399;++i)steps.emplace_back(3005+i*5,SampleResult::Valid);
  run();
  check(!observed.back().calibrated&&observed.back().calibrationId==9,
    "fault during explicit recalibration cannot restore the discarded old calibration");

  steps.clear();
  for(unsigned i=0;i<600;++i)steps.emplace_back(1000+i*5,SampleResult::Valid,
    Vec3{-.298f,-.375f,.917f},Vec3{.2f,0,0});
  run();
  check(!observed.back().calibrated&&observed.back().calibrationCount==0&&
    observed.back().calibrationRejects==600&&
    observed.back().calibrationReason==instrument::StationaryCalibration::Rejection::GyroRate,
    "reported zero-progress startup exposes gyro rejection without falsely completing calibration");
  check(observed.back().imuInvalidSamples==0&&observed.back().imuSampleGaps==0,
    "valid but nonstationary readings are not reported as bus failures");
  for(unsigned i=0;i<400;++i)steps.emplace_back(4000+i*5,SampleResult::Valid);
  steps[600].calibrationId=12;run();
  check(observed.back().calibrated&&observed.back().calibrationRejects==0&&
    observed.back().calibrationPeak==400&&
    observed.back().calibrationReason==instrument::StationaryCalibration::Rejection::None,
    "new calibration request clears old blockage diagnostics and completes normally");
  calibrated();steps.emplace_back(4000,SampleResult::Valid);steps.back().selfTest=true;
  run();
  check(!observed.back().calibrated&&observed.back().hardware.selfTestId==1,
    "explicit IMU self-test invalidates the old calibration even after a failed self-test read");
  check(!observed.back().hardware.testing&&observed.back().hardware.accelSelf.code==3,
    "failed self-test reports communication failure and leaves test mode");
  for(unsigned i=0;i<400;++i)steps.emplace_back(4005+i*5,SampleResult::Valid);
  run();
  check(observed.back().calibrated&&near(observed.back().roll,0),
    "sampler restores normal operation and requires a fresh stationary zero after self-test");
  std::printf("%d failures\n",failures);return failures?1:0;
}
