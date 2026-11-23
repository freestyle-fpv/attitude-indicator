#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <Arduino_GFX_Library.h>
#include <SensorQMI8658.hpp>
#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <cmath>
#include <cstring>
#include "board_config.h"
#include "attitude.h"
#include "renderer.h"
#include "diagnostics.h"
#include "imu_diagnostics.h"
#include "fps_overlay.h"
#include "fast_panel.h"
#include "display_pipeline.h"
#include "circle_band.h"
#include "airspeed_renderer.h"
#include "altimeter_renderer.h"
#include "hud_renderer.h"
#include "instrument_controls.h"
#include "touch_gesture.h"
#include "calibration_controls.h"
#include "calibration_store.h"
#include "instrument_settings.h"

namespace instrument { extern uint32_t renderStages[4]; }
using instrument::Vec3;
using board::SampleResult;
using instrument::UsbSide;
using instrument::SettingsPage;


Arduino_ESP32QSPI displayBus(board::LcdCs,board::LcdClock,board::LcdD0,
                           board::LcdD1,board::LcdD2,board::LcdD3,true);
// Official board sample uses column offset 6 with this panel.
instrument::PanelDisplay display(&displayBus,board::LcdReset,0,466,466,board::LcdColumnOffset,board::LcdRowOffset,0,0);
SensorQMI8658 imu;
XPowersPMU pmu;
instrument::Renderer renderer(true);
instrument::AirspeedRenderer airspeedRenderer(true);
instrument::AltimeterRenderer altimeterRenderer(true);
instrument::HudRenderer hudRenderer(true);
instrument::InstrumentControls instrumentControls;
instrument::FastPanel fastPanel;
instrument::DisplayPipeline displayPipeline;
instrument::FpsOverlay fpsOverlay(true);
uint16_t* frames[instrument::DisplayBufferCount]={};
QueueHandle_t snapshots=nullptr,commands=nullptr;
QueueHandle_t calibrationSaves=nullptr;
instrument::CalibrationRecord initialCalibration;
// -1 storage unavailable/failed, 0 no current record, 1 restored,
// 2 save pending, 3 saved and read back successfully.
std::atomic<uint32_t> calibrationStoreStatus{0};
struct CalibrationRequest {uint32_t id;UsbSide side;};
struct TouchAction {uint32_t context;int16_t x,y;int8_t direction;};
QueueHandle_t touchActions=nullptr;
instrument::SettingsMenu settingsMenu;
Preferences preferences;
bool preferencesReady=false;
UsbSide initialSide=UsbSide::Left,requestedImuSide=UsbSide::Left;
// Low bits hold the orientation and whether tap mode is active. The counter
// identifies the page so an already queued gesture cannot affect a later page.
std::atomic<uint32_t> touchContext{0};
std::atomic<bool> menuClick{false};
std::atomic<bool> touchPending{false};
std::atomic<uint32_t> touchPackets{0},touchErrors{0},touchPosition{0},swipeCount{0};
bool touchReady=false,powerButtonReady=false;
instrument::DiagnosticLease diagnosticLease;
std::atomic<bool> hardwareTelemetryActive{false};
std::atomic<bool> imuSelfTestRequested{false};
std::atomic<uint32_t> bootClicks{0};
instrument::HardwareState imuHardware;
struct MemoryDiagnostic {int ram=-1,psram=-1,flash=-1;uint32_t id=0,ramBytes=0,psramBytes=0;bool pending=false;} memoryDiagnostic;
struct CardDiagnostic {uint32_t id=0;int result=-1;uint64_t bytes=0;bool pending=false;} cardDiagnostic;

bool checkedRead(uint8_t address,uint8_t reg,uint8_t* data,size_t size){
  Wire.beginTransmission(address);Wire.write(reg);
  if(Wire.endTransmission(false)!=0){if(address==board::ImuAddress)++imuHardware.nack;return false;}
  if(Wire.requestFrom(address,size,true)!=size){if(address==board::ImuAddress)++imuHardware.shortRead;return false;}
  for(size_t i=0;i<size;++i)data[i]=Wire.read();return true;
}
struct ImuTestBus{
  bool read(uint8_t reg,uint8_t* data,size_t n){return checkedRead(board::ImuAddress,reg,data,n);}
  bool write(uint8_t reg,uint8_t value){Wire.beginTransmission(board::ImuAddress);Wire.write(reg);Wire.write(value);if(Wire.endTransmission()!=0){++imuHardware.nack;return false;}return true;}
  void wait(){vTaskDelay(pdMS_TO_TICKS(5));}
};
void pollHardware(uint32_t now){
  static uint32_t last=0;if(!hardwareTelemetryActive.load()||uint32_t(now-last)<1000)return;last=now;
  uint8_t data[9];
  if(checkedRead(board::ImuAddress,0,data,9)){
    imuHardware.who=data[0];imuHardware.revision=data[1];imuHardware.ctrl2=data[3];imuHardware.ctrl3=data[4];imuHardware.ctrl7=data[8];
  }else{imuHardware.who=imuHardware.revision=imuHardware.ctrl2=imuHardware.ctrl3=imuHardware.ctrl7=-1;}
  imuHardware.altWho=imuHardware.who!=5&&checkedRead(0x6A,0,data,1)?int(data[0]):-1;
  imuHardware.touch=probeTouch()?1:0;
  uint8_t pins=0;imuHardware.expander=expanderRead(0,pins)?1:0;
  imuHardware.button=powerButtonReady;imuHardware.pwrDown=(pins&board::PowerButtonMask)!=0;
  imuHardware.bootDown=digitalRead(board::Boot)==LOW;imuHardware.bootClicks=bootClicks.load();
  static uint32_t rtcChanged=0;static int rtcPrevious=-1;
  imuHardware.rtc=checkedRead(0x51,0,data,7)?1:0;
  if(imuHardware.rtc==1){
    const uint8_t second=data[4]&0x7f;
    imuHardware.rtcSec=(second&15)<=9&&(second>>4)<=5?(second>>4)*10+(second&15):-1;
    imuHardware.rtcOs=bool(data[4]&0x80);imuHardware.rtcStop=bool(data[0]&0x20);
    if(imuHardware.rtcSec!=rtcPrevious){imuHardware.rtcTick=rtcPrevious<0?-1:1;rtcPrevious=imuHardware.rtcSec;rtcChanged=now;}
    else if(uint32_t(now-rtcChanged)>3000)imuHardware.rtcTick=0;
  }
  imuHardware.dac=checkedRead(0x18,0,data,1)?1:0;
  imuHardware.microphone=checkedRead(0x40,0,data,1)?1:0;
  imuHardware.pmu=checkedRead(0x34,3,data,1)&&data[0]==0x4A?1:0;
  imuHardware.adc=-1;imuHardware.battery=imuHardware.vbusGood=-1;
  imuHardware.vbusMv=imuHardware.systemMv=imuHardware.batteryMv=0;
  if(imuHardware.pmu==1){
    if(checkedRead(0x34,0,data,2)){imuHardware.battery=bool(data[0]&8);imuHardware.vbusGood=bool(data[0]&32);}
    if(checkedRead(0x34,0x30,data,1))imuHardware.adc=data[0];
    if(checkedRead(0x34,0x34,data,8)){
      if(imuHardware.battery==1)imuHardware.batteryMv=((data[0]&0x1f)<<8)|data[1];
      if(imuHardware.vbusGood==1)imuHardware.vbusMv=((data[4]&0x3f)<<8)|data[5];
      imuHardware.systemMv=((data[6]&0x3f)<<8)|data[7];
    }else imuHardware.adc=-1;
  }
}
void runMemoryDiagnostic(){
  ++memoryDiagnostic.id;
  auto test=[](size_t bytes,uint32_t caps,int& result,uint32_t& tested){
    auto* memory=static_cast<uint32_t*>(heap_caps_malloc(bytes,caps));
    while(!memory&&bytes>1024){bytes/=2;memory=static_cast<uint32_t*>(heap_caps_malloc(bytes,caps));}
    if(!memory){result=2;tested=0;return;}
    result=instrument::testMemoryBlock(memory,bytes/4)?1:0;tested=bytes;heap_caps_free(memory);
  };
  test(16*1024,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT,memoryDiagnostic.ram,memoryDiagnostic.ramBytes);
  test(64*1024,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT,memoryDiagnostic.psram,memoryDiagnostic.psramBytes);
  uint8_t digest[32];const auto* app=esp_ota_get_running_partition();
  memoryDiagnostic.flash=app&&esp_partition_get_sha256(app,digest)==ESP_OK?1:0;
  memoryDiagnostic.pending=false;
}

instrument::CalibrationSession calibrationSession;
std::atomic<bool> calibrationClick{false};
std::atomic<uint32_t> calibrationClicks{0};

void runCardDiagnostic(){
  ++cardDiagnostic.id;cardDiagnostic.bytes=0;cardDiagnostic.result=2;
  sdmmc_host_t host=SDMMC_HOST_DEFAULT();host.flags=SDMMC_HOST_FLAG_1BIT;host.max_freq_khz=4000;
  sdmmc_slot_config_t slot=SDMMC_SLOT_CONFIG_DEFAULT();slot.width=1;slot.clk=GPIO_NUM_2;slot.cmd=GPIO_NUM_1;slot.d0=GPIO_NUM_3;
  slot.d1=slot.d2=slot.d3=slot.d4=slot.d5=slot.d6=slot.d7=GPIO_NUM_NC;
  if(sdmmc_host_init()==ESP_OK){
    sdmmc_card_t card={};
    if(sdmmc_host_init_slot(host.slot,&slot)==ESP_OK&&sdmmc_card_init(&host,&card)==ESP_OK){
      cardDiagnostic.bytes=uint64_t(card.csd.capacity)*512;
      auto* sector=heap_caps_malloc(512,MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA);
      cardDiagnostic.result=sector&&sdmmc_read_sectors(&card,sector,0,1)==ESP_OK?1:0;
      if(sector)heap_caps_free(sector);
    }
    sdmmc_host_deinit();
  }
  cardDiagnostic.pending=false;
}

bool expanderRead(uint8_t reg,uint8_t& value){
  Wire.beginTransmission(board::ExpanderAddress);Wire.write(reg);
  if(Wire.endTransmission(false)!=0||Wire.requestFrom(board::ExpanderAddress,size_t(1),true)!=1)return false;
  value=Wire.read();return true;
}
bool expanderWrite(uint8_t reg,uint8_t value){
  Wire.beginTransmission(board::ExpanderAddress);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;
}
void initializePowerButton(){
  uint8_t config=0,polarity=0;
  powerButtonReady=expanderRead(3,config)&&expanderRead(2,polarity)&&
    expanderWrite(3,config|board::PowerButtonMask)&&expanderWrite(2,polarity&~board::PowerButtonMask);
  Serial.printf("PWR calibration button EXIO4: %s; BOOT click opens settings\n",powerButtonReady?"ready":"unavailable");
}
// After setup only samplingTask touches I2C. Detect clicks here at 100 Hz,
// independently of render rate, then hand the action to the UI thread.
void pollPowerButton(uint32_t now){
  static instrument::CalibrationButton button;static uint32_t last=0,retry=0;
  if(uint32_t(now-last)<10)return;last=now;
  if(!powerButtonReady){if(uint32_t(now-retry)<1000)return;retry=now;initializePowerButton();}
  uint8_t pins=0;const bool valid=powerButtonReady&&expanderRead(0,pins);
  if(!valid)powerButtonReady=false;
  if(button.update(valid,(pins&board::PowerButtonMask)!=0,now)){
    calibrationClicks.fetch_add(1);calibrationClick.store(true,std::memory_order_release);
  }
}
std::atomic<bool> panelReady{false};

void IRAM_ATTR touchInterrupt(){touchPending.store(true,std::memory_order_relaxed);}
bool probeTouch(){Wire.beginTransmission(board::TouchAddress);return Wire.endTransmission()==0;}
void initializeTouch(){
  pinMode(board::TouchReset,OUTPUT);digitalWrite(board::TouchReset,LOW);delay(10);
  digitalWrite(board::TouchReset,HIGH);delay(100);
  pinMode(board::TouchInterrupt,INPUT_PULLUP);
  touchReady=probeTouch();
  attachInterrupt(digitalPinToInterrupt(board::TouchInterrupt),touchInterrupt,FALLING);
  touchPending.store(true,std::memory_order_relaxed);
  Serial.printf("CST9217 touch 0x5A: %s; up=next, down=previous\n",touchReady?"present":"not found");
}
// Only the sampling task accesses I2C after setup, including touch reports.
void pollTouch(uint32_t now){
  static instrument::VerticalSwipe gesture;
  static instrument::TouchTap tap;
  static uint32_t seenContext=0;
  static uint32_t last=0,retry=0;static bool contact=false,awaitRelease=false;
  if(uint32_t(now-last)<20)return;last=now;
  const uint32_t context=touchContext.load();
  if(context!=seenContext){
    if(contact){gesture.cancel();tap.cancel();awaitRelease=true;}
    seenContext=context;
  }
  if(!touchReady){if(uint32_t(now-retry)<1000)return;retry=now;touchReady=probeTouch();if(!touchReady)return;touchPending.store(true);}
  const bool pending=touchPending.exchange(false,std::memory_order_relaxed);
  if(!pending&&!contact&&digitalRead(board::TouchInterrupt)!=LOW)return;
  uint8_t bytes[15];bool ok=true;
  Wire.beginTransmission(board::TouchAddress);Wire.write(0xD0);Wire.write(0);
  if(Wire.endTransmission(false)!=0)ok=false;
  if(ok){if(Wire.requestFrom(board::TouchAddress,sizeof(bytes),true)!=sizeof(bytes))ok=false;
    else for(auto& b:bytes)b=Wire.read();}
  if(ok){Wire.beginTransmission(board::TouchAddress);Wire.write(0xD0);Wire.write(0);Wire.write(0xAB);ok=Wire.endTransmission()==0;}
  if(!ok){touchErrors.fetch_add(1);touchReady=false;retry=now;contact=false;gesture.cancel();tap.cancel();return;}
  instrument::TouchReport report;
  if(!instrument::decodeTouch(bytes,sizeof(bytes),report)){gesture.cancel();tap.cancel();contact=false;return;}
  if(report.contacts==1){const auto p=board::touchPoint(report.x,report.y,UsbSide(context&3));report.x=p.x;report.y=p.y;}
  touchPackets.fetch_add(1,std::memory_order_relaxed);contact=report.contacts!=0;
  touchPosition.store((uint32_t(report.contacts)<<18)|(uint32_t(report.x)<<9)|uint32_t(report.y),std::memory_order_relaxed);
  if(awaitRelease && contact)return;
  awaitRelease=false;
  if(context&4u){
    gesture.update(instrument::TouchReport(),now);
    if(tap.update(report,now)){
      const TouchAction action={context,int16_t(tap.start.x),int16_t(tap.start.y),0};
      xQueueSend(touchActions,&action,0);
    }
  }else{
    tap.update(instrument::TouchReport(),now);
    const int8_t direction=gesture.update(report,now);
    if(direction){const TouchAction action={context,0,0,direction};xQueueSend(touchActions,&action,0);}
  }
}

void initializePanel(void* waiter) {
  // SPI's AUTO interrupt affinity follows the initializing core. Keep it with
  // the display consumer so task wakeups cannot outrun the SPI ISR's bus exit.
  const bool ready=display.begin(40000000)&&fastPanel.begin();
  if(ready){
    display.setBrightness(board::Brightness);
    display.fillScreen(RGB565_BLACK);
  }
  panelReady.store(ready,std::memory_order_release);
  xTaskNotifyGive(static_cast<TaskHandle_t>(waiter));
  vTaskDelete(nullptr);
}

// Exact-length checked I2C reads avoid treating an I2C error as data-ready.
bool readImuRegisters(uint8_t reg,uint8_t* data,size_t size) {
  return checkedRead(board::ImuAddress,reg,data,size);
}

bool initializeImu() {
  if(!imu.begin(Wire,board::ImuAddress,board::Sda,board::Scl))return false;
  Wire.setClock(400000);
  Wire.setTimeOut(20);
  if(imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
      SensorQMI8658::ACC_ODR_250Hz,SensorQMI8658::LPF_MODE_2)!=0)return false;
  if(imu.configGyroscope(SensorQMI8658::GYR_RANGE_1024DPS,
      SensorQMI8658::GYR_ODR_224_2Hz,SensorQMI8658::LPF_MODE_2)!=0)return false;
  return imu.enableAccelerometer()&&imu.enableGyroscope();
}

SampleResult readSample(Vec3& acceleration,Vec3& gyro,Vec3& rawAcceleration,UsbSide side) {
  uint8_t status=0,packet[17];
  if(!readImuRegisters(0x2E,&status,1))return SampleResult::Invalid;
  if((status&3)!=3){++imuHardware.notReady;return SampleResult::NotReady;}
  // AX..AZ and GX..GZ occupy one contiguous 12-byte register block.
  if(!readImuRegisters(0x30,packet,sizeof(packet)))return SampleResult::Invalid;
  const uint32_t stamp=uint32_t(packet[0])|(uint32_t(packet[1])<<8)|(uint32_t(packet[2])<<16);
  static bool haveStamp=false;
  if(haveStamp&&stamp==imuHardware.timestamp)++imuHardware.repeatedTimestamp;
  haveStamp=true;imuHardware.timestamp=stamp;
  imuHardware.temperature=int16_t(uint16_t(packet[3])|(uint16_t(packet[4])<<8))/256.f;
  const uint8_t* bytes=packet+5;
  float values[6];
  for(int i=0;i<6;++i){
    const int16_t raw=static_cast<int16_t>(static_cast<uint16_t>(bytes[2*i])|
                                         (static_cast<uint16_t>(bytes[2*i+1])<<8));
    values[i]=raw*(i<3 ? 4.0f/32768.0f : 1024.0f/32768.0f*board::RadPerDegree);
  }
  rawAcceleration={values[0],values[1],values[2]};
  acceleration=board::toBody(rawAcceleration,side);
  gyro=board::toBody({values[3],values[4],values[5]},side);
  // Saturated readings invalidate attitude instead of integrating clipped rates.
  for(int i=0;i<3;++i)if(std::fabs(values[i])>3.95f){++imuHardware.saturated;return SampleResult::Invalid;}
  for(int i=3;i<6;++i)if(std::fabs(values[i])>17.7f){++imuHardware.saturated;return SampleResult::Invalid;}
  return SampleResult::Valid;
}

void samplingTask(void*) {
  instrument::AttitudeFilter filter;
  instrument::StationaryCalibration calibration;
  Vec3 bias{0,0,0};
  Snapshot state;
  UsbSide side=initialSide;
  bool restorePending=instrument::validCalibrationRecord(initialCalibration,side);
  uint32_t storageEpoch=0;
  instrument::CalibrationButton bootButton;
  bool initialized=false,havePrevious=false;
  uint32_t lastAttempt=millis()-1000,previousMicros=0;
  TickType_t wake=xTaskGetTickCount();
  for(;;){
    CalibrationRequest command;
    if(xQueueReceive(commands,&command,0)==pdTRUE){
      restorePending=false;calibrationStoreStatus.store(++storageEpoch<<3);
      state.calibrationId=command.id;
      side=command.side;
      state.calibrated=false;
      state.calibrationCount=0;
      state.calibrationPeak=0;
      state.calibrationRejects=0;
      state.calibrationReason=instrument::StationaryCalibration::Rejection::None;
      calibration.reset();
      havePrevious=false;
    }
    if(!initialized&&static_cast<uint32_t>(millis()-lastAttempt)>=1000){
      lastAttempt=millis();
      initialized=initializeImu();
      state.present=initialized;
      // Reconfiguring the same IMU does not invalidate bias or installation trim.
      calibration.reset();
      if(!state.calibrated)state.calibrationCount=0;
      state.sampleFault=true;
      havePrevious=false;
      state.sampledAt=0;
    }
    if(imuSelfTestRequested.exchange(false)){
      restorePending=false;calibrationStoreStatus.store(++storageEpoch<<3);
      imuHardware.testing=true;++imuHardware.selfTestId;
      state.sampleFault=true;state.hardware=imuHardware;xQueueOverwrite(snapshots,&state);
      ImuTestBus bus;
      imuHardware.accelSelf=instrument::imuSelfTest(bus,false);
      imuHardware.gyroSelf=instrument::imuSelfTest(bus,true);
      initialized=initializeImu();lastAttempt=millis();
      imuHardware.restored=initialized&&imuHardware.accelSelf.restored&&imuHardware.gyroSelf.restored;
      imuHardware.testing=false;state.present=initialized;state.sampleFault=true;state.calibrated=false;
      state.calibrationCount=state.calibrationPeak=0;state.calibrationRejects=0;
      state.calibrationReason=instrument::StationaryCalibration::Rejection::None;
      calibration.reset();havePrevious=false;wake=xTaskGetTickCount();
    }
    pollHardware(millis());
    Vec3 acceleration,gyro,raw;
    const SampleResult result=initialized?readSample(acceleration,gyro,raw,side):SampleResult::NotReady;
    if(result==SampleResult::Invalid){
      ++state.imuInvalidSamples;
      // Hide the pose immediately, but retain bias, trim and filter orientation.
      // The first recovered sample starts a new interval; missing motion is lost.
      state.sampleFault=true;
      if(!state.calibrated)state.calibrationCount=0;
      calibration.reset();
      havePrevious=false;
      const uint32_t age=state.sampledAt?static_cast<uint32_t>(millis()-state.sampledAt):static_cast<uint32_t>(millis()-lastAttempt);
      if(age>500){initialized=false;state.present=false;}
    }else if(result==SampleResult::Valid){
      state.sampleFault=false;
      const uint32_t now=micros();
      const float dt=havePrevious?static_cast<uint32_t>(now-previousMicros)*1e-6f:0;
      previousMicros=now;
      havePrevious=true;
      state.sampledAt=millis();
      ++state.samples;
      state.rawAcceleration=raw;
      state.gyroDps={gyro.x/board::RadPerDegree,gyro.y/board::RadPerDegree,gyro.z/board::RadPerDegree};
      if(dt>0.1f){
        ++state.imuSampleGaps;
        if(!state.calibrated)calibration.reset();
      }
      if(restorePending){
        if(instrument::restoreCalibration(filter,initialCalibration,side,acceleration,bias)){
          restorePending=false;state.calibrated=true;
          state.calibrationCount=state.calibrationPeak=instrument::StationaryCalibration::RequiredSamples;
          state.calibrationReason=instrument::StationaryCalibration::Rejection::None;
          calibrationStoreStatus.store((storageEpoch<<3)|1u);
        }
        // Establish a new interval before authorizing the restored pose.
        state.sampleFault=true;
      }else if(!state.calibrated){
        if(calibration.add(acceleration,gyro)){
          bias=calibration.bias();
          filter.reset(calibration.acceleration());
          filter.zeroReference();
          state.calibrated=true;
          state.calibrationReason=instrument::StationaryCalibration::Rejection::None;
          const instrument::CalibrationSaveRequest save={instrument::makeCalibrationRecord(side,bias,calibration.acceleration()),(storageEpoch<<3)|2u};
          calibrationStoreStatus.store(save.token);xQueueOverwrite(calibrationSaves,&save);
        }
        state.calibrationCount=calibration.count();
        if(state.calibrationCount>state.calibrationPeak)state.calibrationPeak=state.calibrationCount;
        if(calibration.rejection()!=instrument::StationaryCalibration::Rejection::None){
          ++state.calibrationRejects;
          state.calibrationReason=calibration.rejection();
        }
      }else if(dt>0&&dt<=0.1f){
        state.sampleFault=!filter.update(acceleration,{gyro.x-bias.x,gyro.y-bias.y,gyro.z-bias.z},dt);
        if(state.sampleFault)havePrevious=false;
      }else{
        // No trustworthy integration interval yet. Resume on the next sample
        // without redefining zero from an arbitrary in-flight acceleration.
        state.sampleFault=true;
      }
      state.pitch=filter.pitch();
      state.roll=filter.roll();
    }else if(initialized){
      const uint32_t age=state.sampledAt?static_cast<uint32_t>(millis()-state.sampledAt):static_cast<uint32_t>(millis()-lastAttempt);
      if(age>100){
        if(havePrevious)++state.imuSampleGaps;
        state.sampleFault=true;havePrevious=false;calibration.reset();
        if(!state.calibrated)state.calibrationCount=0;
      }
      if(age>500){initialized=false;state.present=false;}
    }
    state.hardware=imuHardware;
    xQueueOverwrite(snapshots,&state);
    pollTouch(millis());
    pollPowerButton(millis());
    if(bootButton.update(true,digitalRead(board::Boot)==LOW,millis())){bootClicks.fetch_add(1);menuClick.store(true);}
    vTaskDelayUntil(&wake,pdMS_TO_TICKS(5));
  }
}

void centeredText(const char* text,int y,int size,uint16_t color) {
  display.setTextSize(size);
  display.setTextColor(color);
  display.setCursor((466-static_cast<int>(strlen(text))*6*size)/2,y);
  display.print(text);
}

void stopWithMessage(const char* message) {
  Serial.println(message);
  display.fillScreen(RGB565_BLACK);
  centeredText(message,210,2,RGB565_RED);
  for(;;)delay(1000);
}

void applyControl(instrument::InstrumentControls::Action action) {
  if(action==instrument::InstrumentControls::None)return;
  if(action==instrument::InstrumentControls::Calibrate){
    const auto side=settingsMenu.options.side;
    const CalibrationRequest command={calibrationSession.request(side!=requestedImuSide),side};
    if(!command.id)return;
    requestedImuSide=side;
    xQueueOverwrite(commands,&command);
    Serial.printf("CALIBRATION start id=%lu; keep installation level and still\n",(unsigned long)command.id);
  }
  displayPipeline.invalidate();
}

void updateMenu(uint8_t previousOptions){
  const auto options=settingsMenu.options;
  const bool open=settingsMenu.page!=SettingsPage::Closed;
  touchContext.store(((touchContext.load()&~7u)+8u)|unsigned(options.side)|(open?4u:0u));
  displayPipeline.invalidate();
  if(options.encode()!=previousOptions&&preferencesReady&&preferences.putUChar("display",options.encode())!=1)
    Serial.println("MENU settings could not be saved");
  Serial.printf("MENU page=%u usb=%s fps=%d\n",unsigned(settingsMenu.page),instrument::SettingsMenu::sideLabel(options.side),options.fps);
  if(!open&&options.side!=requestedImuSide)applyControl(instrumentControls.shortPress());
}

void clickMenu(){settingsMenu.click();updateMenu(settingsMenu.options.encode());}
void tapMenu(int x,int y){
  const uint8_t previous=settingsMenu.options.encode();
  if(settingsMenu.tap(x,y))updateMenu(previous);
}

void controls() {
  const uint32_t now=millis();
  if(menuClick.exchange(false))clickMenu();
  TouchAction touch;
  if(xQueueReceive(touchActions,&touch,0)==pdTRUE&&touch.context==touchContext.load()){
    if(touch.direction){
      applyControl(instrumentControls.step(touch.direction));swipeCount.fetch_add(1);
      Serial.printf("SWIPE %s -> %s\n",touch.direction>0?"up":"down",instrumentControls.airspeed()?"airspeed":instrumentControls.attitude()?"attitude":instrumentControls.altimeter()?"altimeter":"hud");
    }else tapMenu(touch.x,touch.y);
  }
  if(calibrationClick.exchange(false,std::memory_order_acquire)){
    if(settingsMenu.page!=SettingsPage::Closed){settingsMenu.page=SettingsPage::Closed;updateMenu(settingsMenu.options.encode());}
    applyControl(instrumentControls.shortPress());
  }
  // Bound command processing so a continuously streaming host cannot starve drawing.
  for(int count=0;count<64&&Serial.available();++count){
    const char key=static_cast<char>(Serial.read());
    if(key=='J'){diagnosticLease.renew(now);continue;}
    if(key=='K'){diagnosticLease.stop();continue;}
    if(key=='S'){imuSelfTestRequested.store(true);continue;}
    if(key=='M'){memoryDiagnostic.pending=true;continue;}
    if(key=='N'){cardDiagnostic.pending=true;continue;}
    if(key=='c'){
      if(settingsMenu.page!=SettingsPage::Closed){settingsMenu.page=SettingsPage::Closed;updateMenu(settingsMenu.options.encode());}
      applyControl(instrumentControls.shortPress());continue;
    }
    if(key=='m')clickMenu();
    else if(settingsMenu.page==SettingsPage::Closed)applyControl(instrumentControls.input(key,now));
    else if(key>='1' && key<=(settingsMenu.page==SettingsPage::Main?'2':'4')){
      const auto row=instrument::SettingsMenu::row(settingsMenu.page,key-'1');
      tapMenu(row.x+row.width/2,row.y+row.height/2);
    }
  }
}

// Read-only snapshots; the USB/render thread never accesses I2C.
void emitServiceDiagnostics(const Snapshot& state){
  const uint32_t now=millis();const bool active=diagnosticLease.active(now);hardwareTelemetryActive.store(active);if(!active)return;
  static uint32_t lastFast=0,lastSamples=0,lastHardware=0,txDrops=0;
  static instrument::DiagnosticCadence cadence;
  static unsigned part=0;
  char line[480];int n=0;
  if(cadence.due(now)){
    const float hz=(state.samples-lastSamples)*1000.f/uint32_t(now-lastFast);
    lastSamples=state.samples;lastFast=now;
    n=snprintf(line,sizeof(line),"IMUFAST seq=%lu present=%d fault=%d valid=%d cal=%u/400 reason=%s accel_g=%.4f,%.4f,%.4f gyro_dps=%.3f,%.3f,%.3f g=%.3f hz=%.1f age_ms=%lu\n",
      (unsigned long)state.samples,state.present,state.sampleFault,instrument::currentAttitude(state,now),state.calibrationCount,
      instrument::calibrationReasonName(state.calibrationReason),state.rawAcceleration.x,state.rawAcceleration.y,state.rawAcceleration.z,
      state.gyroDps.x,state.gyroDps.y,state.gyroDps.z,std::sqrt(state.rawAcceleration.x*state.rawAcceleration.x+state.rawAcceleration.y*state.rawAcceleration.y+state.rawAcceleration.z*state.rawAcceleration.z),hz,
      (unsigned long)(state.samples?uint32_t(now-state.sampledAt):0));
    if(n<=0||n>=int(sizeof(line))||!instrument::tryDiagnostic(Serial,line,n))++txDrops;
  }
  if(!part&&uint32_t(now-lastHardware)>=1000){part=1;lastHardware=now;}
  const auto& h=state.hardware;
  switch(part){
    case 1:n=snprintf(line,sizeof(line),"IMUHW who=%d alt_who=%d rev=%d ctrl2=%d ctrl3=%d ctrl7=%d temp_c=%.2f nack=%lu short=%lu sat=%lu notready=%lu repeats=%lu timestamp=%lu restore=%d cal_max_dps=%.1f cal_var_dps=%.2f cal_step_g=%.3f cal_mean_g=%.3f cal_store=%d\n",
      h.who,h.altWho,h.revision,h.ctrl2,h.ctrl3,h.ctrl7,h.temperature,(unsigned long)h.nack,(unsigned long)h.shortRead,(unsigned long)h.saturated,(unsigned long)h.notReady,(unsigned long)h.repeatedTimestamp,(unsigned long)h.timestamp,h.restored,
      instrument::StationaryCalibration::MaxRawRateDps,instrument::StationaryCalibration::MaxRateChangeDps,instrument::StationaryCalibration::MaxInstantAccelerationChangeG,instrument::StationaryCalibration::MaxMeanAccelerationChangeG,instrument::calibrationStorageCode(calibrationStoreStatus.load()));break;
    case 2:n=snprintf(line,sizeof(line),"BOARD pmu=%d adc=%d touch=%d expander=%d panel=%d battery=%d vbus_good=%d vbus_mv=%u sys_mv=%u batt_mv=%u heap=%u heap_min=%u heap_largest=%u psram=%u psram_free=%u flash=%u reset=%d nvs=%d tx_drop=%lu\n",
      h.pmu,h.adc,h.touch,h.expander,panelReady.load(),h.battery,h.vbusGood,h.vbusMv,h.systemMv,h.batteryMv,
      ESP.getFreeHeap(),ESP.getMinFreeHeap(),ESP.getMaxAllocHeap(),ESP.getPsramSize(),ESP.getFreePsram(),ESP.getFlashChipSize(),int(esp_reset_reason()),preferencesReady,(unsigned long)txDrops);break;
    case 3:{const uint32_t p=touchPosition.load();n=snprintf(line,sizeof(line),"INPUT touch_packets=%lu touch_errors=%lu x=%u y=%u contacts=%u swipes=%lu boot_clicks=%lu pwr_clicks=%lu boot_down=%d pwr_down=%d button_ready=%d\n",
      (unsigned long)touchPackets.load(),(unsigned long)touchErrors.load(),unsigned((p>>9)&511),unsigned(p&511),unsigned(p>>18),(unsigned long)swipeCount.load(),(unsigned long)h.bootClicks,(unsigned long)calibrationClicks.load(),h.bootDown,h.pwrDown,h.button);break;}
    case 4:{const auto stats=displayPipeline.stats();n=snprintf(line,sizeof(line),"DISPLAY fps=%.1f panel=%d te_us=%lu te_pulses=%lu te_miss=%lu\n",stats.fps,panelReady.load(),(unsigned long)stats.tePeriodUs,(unsigned long)stats.tePulses,(unsigned long)stats.teMisses);break;}
    case 5:n=snprintf(line,sizeof(line),"MEMORY id=%lu ram=%d ram_bytes=%lu psram=%d psram_bytes=%lu flash=%d pending=%d\n",
      (unsigned long)memoryDiagnostic.id,memoryDiagnostic.ram,(unsigned long)memoryDiagnostic.ramBytes,memoryDiagnostic.psram,(unsigned long)memoryDiagnostic.psramBytes,memoryDiagnostic.flash,memoryDiagnostic.pending);break;
    case 6:n=snprintf(line,sizeof(line),"SELFTEST id=%lu running=%d accel=%d gyro=%d restore=%d accel_mg=%.2f,%.2f,%.2f gyro_dps=%.2f,%.2f,%.2f\n",
      (unsigned long)h.selfTestId,h.testing,h.accelSelf.code,h.gyroSelf.code,h.restored,h.accelSelf.response.x,h.accelSelf.response.y,h.accelSelf.response.z,h.gyroSelf.response.x,h.gyroSelf.response.y,h.gyroSelf.response.z);break;
    case 7:n=snprintf(line,sizeof(line),"PERIPHERAL rtc=%d rtc_sec=%d rtc_os=%d rtc_stop=%d rtc_tick=%d dac=%d mic=%d\n",h.rtc,h.rtcSec,h.rtcOs,h.rtcStop,h.rtcTick,h.dac,h.microphone);break;
    case 8:n=snprintf(line,sizeof(line),"SDCHECK id=%lu pending=%d result=%d bytes=%llu\n",(unsigned long)cardDiagnostic.id,cardDiagnostic.pending,cardDiagnostic.result,(unsigned long long)cardDiagnostic.bytes);break;
    default:return;
  }
  if(n<=0||n>=int(sizeof(line))){++txDrops;part=0;return;}
  if(instrument::tryDiagnostic(Serial,line,n)){if(++part>8)part=0;}
}

void setup() {
  // HWCDC defaults to 256 bytes; complete per-frame diagnostics can exceed that.
  // Allocate once at startup while retaining zero-wait writes during rendering.
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  instrument::configureDiagnostics(Serial);
  Serial.printf("Firmware: %s\n",instrument::FirmwareVersion);
  Serial.printf("CPU MHz: %u; band rows: %u; DMA chunk pixels: %u\n",getCpuFrequencyMhz(),instrument::DisplayBandRows,static_cast<unsigned>(fastPanel.chunkPixels()));
  pinMode(board::Boot,INPUT_PULLUP);
  preferencesReady=preferences.begin("instrument-ui");
  if(preferencesReady)settingsMenu.options=instrument::DisplayOptions::decode(preferences.getUChar("display",4));
  else Serial.println("MENU settings storage unavailable");
  initialSide=requestedImuSide=settingsMenu.options.side;
  if(preferencesReady&&instrument::loadCalibration(preferences,initialSide,initialCalibration)){
    calibrationStoreStatus.store(0);
    Serial.println("CALIBRATION stored zero and bias found; restore after fresh IMU sample");
  }else{
    calibrationStoreStatus.store(preferencesReady?0u:4u);
    Serial.println("CALIBRATION no valid stored record; keep installation level and still for first calibration");
  }
  touchContext.store(unsigned(initialSide));
  Wire.begin(board::Sda,board::Scl,400000);
  Wire.setTimeOut(20);
  // Preserve the factory rail voltages and charging configuration.
  const bool pmuPresent=pmu.init(Wire,board::Sda,board::Scl);
  Serial.printf("AXP2101: %s\n",pmuPresent?"present":"not found");
  if(pmuPresent){pmu.disableLongPressShutdown();pmu.enableSystemVoltageMeasure();pmu.enableVbusVoltageMeasure();pmu.enableBattVoltageMeasure();}
  initializePowerButton();
  if(xTaskCreatePinnedToCore(initializePanel,"panel_init",6144,
      xTaskGetCurrentTaskHandle(),1,nullptr,0)!=pdPASS){
    Serial.println("Display task initialization failed");
    for(;;)delay(1000);
  }
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  if(!panelReady.load(std::memory_order_acquire)){
    Serial.println("Display initialization failed");
    for(;;)delay(1000);
  }
  if(!psramFound())stopWithMessage("ENABLE OPI PSRAM");
  for(auto& frame:frames)frame=static_cast<uint16_t*>(heap_caps_aligned_alloc(16,466*instrument::DisplayBandRows*sizeof(uint16_t),MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL));
  for(auto frame:frames)if(!frame)stopWithMessage("NO FRAME MEMORY");
  if(!renderer.setInstallation(initialSide))stopWithMessage("NO FRAME MEMORY");
  if(!airspeedRenderer.begin())stopWithMessage("NO AIRSPEED MEMORY");
  if(!altimeterRenderer.begin())stopWithMessage("NO ALTIMETER MEMORY");
  if(!hudRenderer.begin())stopWithMessage("NO HUD MEMORY");
  initializeTouch();
  snapshots=xQueueCreate(1,sizeof(Snapshot));
  commands=xQueueCreate(1,sizeof(CalibrationRequest));
  calibrationSaves=xQueueCreate(1,sizeof(instrument::CalibrationSaveRequest));
  touchActions=xQueueCreate(4,sizeof(TouchAction));
  if(!snapshots||!commands||!touchActions||!calibrationSaves)stopWithMessage("NO QUEUE MEMORY");
  if(xTaskCreatePinnedToCore(samplingTask,"attitude",6144,nullptr,3,nullptr,0)!=pdPASS)
    stopWithMessage("IMU TASK FAILED");
  if(!displayPipeline.begin(display,fastPanel,renderer,airspeedRenderer,altimeterRenderer,hudRenderer,frames,snapshots))stopWithMessage("DISPLAY TASK FAILED");
  Serial.println("ATTITUDE (IMU). PWR: shared calibration; BOOT or m: settings; swipe: switch. d: demo; a: airspeed; h: attitude; c: calibrate; v135 + newline: simulated km/h; t: altimeter; u: HUD; b275 + newline: simulated meters (0-3000).");
  Serial.println("Calibration diagnostics: caldiag3; J=20Hz diagnostics, K=stop, S=IMU self-test, M=memory/image check, N=read-only SD check. IMUDIAG at 1 Hz.");
}

void loop() {
  controls();
  instrument::CalibrationSaveRequest completedCalibration;
  if(xQueueReceive(calibrationSaves,&completedCalibration,0)==pdTRUE){
    const bool saved=preferencesReady&&instrument::saveCalibration(preferences,completedCalibration.record);
    instrument::finishCalibrationSave(calibrationStoreStatus,completedCalibration.token,saved);
    Serial.printf("CALIBRATION storage=%s usb=%s; %s\n",saved?"saved":"failed",instrument::SettingsMenu::sideLabel(UsbSide(completedCalibration.record.side)),saved?"reuse on next boot":"current pose works but next boot may require calibration");
  }
  instrumentControls.update(millis());
  Snapshot state;
  xQueuePeek(snapshots,&state,0);
  if(calibrationSession.observe(state.calibrationId,state.calibrated)){
    displayPipeline.invalidate();
    Serial.printf("CALIBRATION complete id=%lu; shared pitch=%.2f roll=%.2f\n",(unsigned long)state.calibrationId,state.pitch,state.roll);
  }
  static uint32_t sequence=0,lastMenuGeneration=0;
  emitServiceDiagnostics(state);
  if(memoryDiagnostic.pending)runMemoryDiagnostic();
  if(cardDiagnostic.pending)runCardDiagnostic();
  const uint32_t generation=displayPipeline.generation();
  if(settingsMenu.page!=SettingsPage::Closed){
    if(generation!=lastMenuGeneration){
      instrument::DisplayJob page;
      page.sequence=++sequence;page.generation=generation;
      page.menu=settingsMenu.page;page.options=settingsMenu.options;
      displayPipeline.startFrame(page,0,0);displayPipeline.submit(displayPipeline.nextBand());
      lastMenuGeneration=generation;
    }
    delay(8);return;
  }
  const bool calibrating=calibrationSession.active();
  const bool airspeed=!calibrating&&instrumentControls.airspeed(),altimeter=!calibrating&&instrumentControls.altimeter(),hud=!calibrating&&instrumentControls.hud(),demo=!calibrating&&instrumentControls.demo();
  const bool current=instrument::currentAttitude(state,millis());
  const bool attitude=!calibrating&&instrumentControls.attitude()&&(demo||current);
  instrument::DisplayStats stats=displayPipeline.stats();
  ++sequence;
  float pitch=state.pitch,roll=state.roll;
  if(instrumentControls.attitude()&&demo){float t=static_cast<uint32_t>(millis()-instrumentControls.demoStarted())*.001f;pitch=20*std::sin(t*.5f);roll=40*std::sin(t*.31f);}
  if(settingsMenu.options.fps)fpsOverlay.update(stats.fps);
  instrument::DisplayJob request;
  request.state=state;request.demo=demo;request.generation=generation;
  request.sequence=sequence;request.attitude=attitude;
  request.options=settingsMenu.options;
  request.fpsTenths=request.options.fps?fpsOverlay.tenths():-1;
  request.altimeter=altimeter;request.altitudeM=instrumentControls.altitude();
  request.airspeed=airspeed;request.speedKmh=instrumentControls.speed();
  request.hud=hud;
  if(hud){
    auto& h=request.hudInput;h.nowMs=millis();
    h.speedKmh=instrumentControls.speed();h.altitudeM=instrumentControls.altitude();h.climb=instrumentControls.climb();
    h.pitch=state.pitch;h.roll=state.roll;
    h.attitudeValid=current;
    // QMI8658 is six-axis: heading and the unconnected air-data fields are simulated.
    const float t=(demo?uint32_t(h.nowMs-instrumentControls.demoStarted()):h.nowMs)*.001f;
    h.heading=180+18*std::sin(t*.15f);
    if(demo){h.attitudeValid=true;h.pitch=5+8*std::sin(t*.3f);h.roll=25*std::sin(t*.4f);
      h.speedKmh=86+16*std::sin(t*.35f);h.altitudeM=275+55*std::sin(t*.2f);h.climb=11*std::cos(t*.2f);}
  }
  displayPipeline.startFrame(request,pitch,roll);
  for(;;){
    instrument::DisplayJob job=displayPipeline.nextBand();
    const unsigned slot=job.slot;
    if(job.stagedRound||job.hud){
      const uint32_t rendered=job.hud?displayPipeline.renderHudRegions(job,false):
        displayPipeline.renderRoundRegions(job,pitch,roll,false);
      const auto completed=displayPipeline.parallelBackgroundDone();
      assert(completed.slot==slot&&completed.sequence==job.sequence);
      job=completed;job.stages[3]=rendered;job.renderUs+=rendered;
      if(job.stagedRound)for(unsigned i=0;i<3;++i)job.stages[i+1]=displayPipeline.workerStages[0][i]+displayPipeline.workerStages[1][i];
    }else if(job.altimeter){
      size_t offset=0;uint32_t backgroundUs=0,foregroundUs=0;
      for(int i=0;i<job.altitudeDamage.count;++i){
        const auto r=job.altitudeDamage.regions[i];
        if(job.altimeter?bool(i&1):!instrument::HudRenderer::backgroundRegion(i)){
          uint32_t tick=micros();
          if(job.hud)displayPipeline.renderHudRegion(job,r,offset,false);
          else {altimeterRenderer.renderBackgroundRectangle(displayPipeline.airspeedPixels(slot)+offset,r);backgroundUs+=micros()-tick;
            tick=micros();altimeterRenderer.renderForegroundPrepared(displayPipeline.airspeedPixels(slot)+offset,slot,r);}
          foregroundUs+=micros()-tick;
        }
        offset+=r.width*r.height;
      }
      const auto completed=displayPipeline.parallelBackgroundDone();
      assert(completed.slot==slot&&completed.sequence==job.sequence);
      job=completed;job.stages[0]+=backgroundUs;job.stages[1]+=foregroundUs;job.renderUs+=backgroundUs+foregroundUs;
      if(job.hud)job.stages[3]=foregroundUs;
    }else if(job.airspeed){
      const uint32_t began=micros();
      airspeedRenderer.renderForegroundRectangle(displayPipeline.airspeedPixels(slot),job.speedKmh,job.patch);
      job.stages[1]=micros()-began;job.renderUs+=job.stages[1];
    }else if(job.attitude){
      uint32_t began=micros();
      const auto rect=job.patch;
      auto* pixels=frames[slot];
      renderer.renderForegroundWindow(pixels,pitch,roll,rect);
      if(job.options.fps)fpsOverlay.blitPanelBand(pixels,rect,job.options.side);
      if(demo)fpsOverlay.blitPanelLegend(pixels,rect,job.options.side,true);
      job.renderUs+=micros()-began;
      for(unsigned i=1;i<4;++i)job.stages[i]=instrument::renderStages[i];
    }
    displayPipeline.submit(job);
    if(job.lastBand)break;
  }
  static uint32_t lastLog=0,lastSamples=0,droppedLogs=0;
  if(static_cast<uint32_t>(millis()-lastLog)>=1000){
    const float sampleHz=(state.samples-lastSamples)*1000.f/(millis()-lastLog);
    lastSamples=state.samples;lastLog=millis();
    {
      char imuMessage[320];
      const int imuLength=instrument::formatImuDiagnostic(imuMessage,sizeof(imuMessage),state,lastLog,sampleHz);
      if(imuLength>0)instrument::tryDiagnostic(Serial,imuMessage,imuLength);
    }
    char message[720];
    const uint32_t tp=touchPosition.load(std::memory_order_relaxed);
    const int length=snprintf(message,sizeof(message),"fps=%.1f pitch=%.1f roll=%.1f valid=%d cal=%u demo=%d raw_g=%.3f,%.3f,%.3f render_ms=%.1f tx_ms=%.1f frame_ms=%.1f max_gap_ms=%.1f sample_hz=%.1f heap=%u drops=%lu setup_ms=%.1f stack=%u,%u,%u stages=%lu,%lu,%lu,%lu mode=%s speed=%.1f altitude_m=%.1f te_us=%lu te_miss=%lu te_pulses=%lu patch=%lu touch=%lu touch_err=%lu touch_xy=%u,%u touch_n=%u swipes=%lu scan_us=%ld,%ld cal_id=%lu cal_ui=%d pwr_clicks=%lu usb=%s round_cross=%lu hud_cross=%lu\n",
      stats.fps,hud?request.hudInput.pitch:state.pitch,hud?request.hudInput.roll:state.roll,current,state.calibrationCount,demo,
      state.rawAcceleration.x,state.rawAcceleration.y,state.rawAcceleration.z,
      stats.renderUs/1000.f,stats.transferUs/1000.f,stats.intervalUs/1000.f,stats.maxGapUs/1000.f,
      sampleHz,ESP.getFreeHeap(),static_cast<unsigned long>(droppedLogs),stats.setupUs/1000.f,
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),stats.stackFree,stats.backgroundStackFree,
      (unsigned long)stats.stages[0],(unsigned long)stats.stages[1],(unsigned long)stats.stages[2],(unsigned long)stats.stages[3],airspeed?"airspeed":altimeter?"altimeter":hud?"hud":"attitude",hud?request.hudInput.speedKmh:instrumentControls.speed(),hud?request.hudInput.altitudeM:instrumentControls.altitude(),(unsigned long)stats.tePeriodUs,(unsigned long)stats.teMisses,(unsigned long)stats.tePulses,(unsigned long)stats.patchPixels,(unsigned long)touchPackets.load(),(unsigned long)touchErrors.load(),unsigned((tp>>9)&511),unsigned(tp&511),unsigned(tp>>18),(unsigned long)swipeCount.load(),(long)stats.scanMinUs,(long)stats.scanMaxUs,(unsigned long)state.calibrationId,calibrating,(unsigned long)calibrationClicks.load(),instrument::SettingsMenu::sideLabel(settingsMenu.options.side),(unsigned long)stats.roundScanCrosses,(unsigned long)stats.hudScanCrosses);
    if(length<=0||length>=static_cast<int>(sizeof(message))||!instrument::tryDiagnostic(Serial,message,length))++droppedLogs;
  }
  // Allow idle/housekeeping regularly without inserting a pause in every image.
  if((sequence&3u)==0)delay(1);
}

// CPU1 now also prepares HUD geometry and dirty-region matching.
SET_LOOP_TASK_STACK_SIZE(12288);
