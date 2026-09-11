#include <Arduino.h>
#include <Wire.h>
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

namespace instrument { extern uint32_t renderStages[4]; }
using instrument::Vec3;
using board::SampleResult;


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
QueueHandle_t swipes=nullptr;
std::atomic<bool> touchPending{false};
std::atomic<uint32_t> touchPackets{0},touchErrors{0},touchPosition{0},swipeCount{0};
bool touchReady=false,powerButtonReady=false;
instrument::CalibrationSession calibrationSession;
std::atomic<bool> calibrationClick{false};
std::atomic<uint32_t> calibrationClicks{0};

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
  Serial.printf("PWR calibration button EXIO4: %s; BOOT runtime actions disabled\n",powerButtonReady?"ready":"unavailable");
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
  static uint32_t last=0,retry=0;static bool contact=false;
  if(uint32_t(now-last)<20)return;last=now;
  if(!touchReady){if(uint32_t(now-retry)<1000)return;retry=now;touchReady=probeTouch();if(!touchReady)return;touchPending.store(true);}
  const bool pending=touchPending.exchange(false,std::memory_order_relaxed);
  if(!pending&&!contact&&digitalRead(board::TouchInterrupt)!=LOW)return;
  uint8_t bytes[15];bool ok=true;
  Wire.beginTransmission(board::TouchAddress);Wire.write(0xD0);Wire.write(0);
  if(Wire.endTransmission(false)!=0)ok=false;
  if(ok){if(Wire.requestFrom(board::TouchAddress,sizeof(bytes),true)!=sizeof(bytes))ok=false;
    else for(auto& b:bytes)b=Wire.read();}
  if(ok){Wire.beginTransmission(board::TouchAddress);Wire.write(0xD0);Wire.write(0);Wire.write(0xAB);ok=Wire.endTransmission()==0;}
  if(!ok){touchErrors.fetch_add(1);touchReady=false;retry=now;contact=false;gesture.cancel();return;}
  instrument::TouchReport report;
  if(!instrument::decodeTouch(bytes,sizeof(bytes),report)){gesture.cancel();contact=false;return;}
  touchPackets.fetch_add(1,std::memory_order_relaxed);contact=report.contacts!=0;
  touchPosition.store((uint32_t(report.contacts)<<18)|(uint32_t(report.x)<<9)|uint32_t(report.y),std::memory_order_relaxed);
  const int8_t direction=gesture.update(report,now);
  if(direction)xQueueSend(swipes,&direction,0);
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
  Wire.beginTransmission(board::ImuAddress);
  Wire.write(reg);
  if(Wire.endTransmission(false)!=0)return false;
  if(Wire.requestFrom(board::ImuAddress,size,true)!=size)return false;
  for(size_t i=0;i<size;++i)data[i]=Wire.read();
  return true;
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

SampleResult readSample(Vec3& acceleration,Vec3& gyro,Vec3& rawAcceleration) {
  uint8_t status=0,bytes[12];
  if(!readImuRegisters(0x2E,&status,1))return SampleResult::Invalid;
  if((status&3)!=3)return SampleResult::NotReady;
  // AX..AZ and GX..GZ occupy one contiguous 12-byte register block.
  if(!readImuRegisters(0x35,bytes,sizeof(bytes)))return SampleResult::Invalid;
  float values[6];
  for(int i=0;i<6;++i){
    const int16_t raw=static_cast<int16_t>(static_cast<uint16_t>(bytes[2*i])|
                                         (static_cast<uint16_t>(bytes[2*i+1])<<8));
    values[i]=raw*(i<3 ? 4.0f/32768.0f : 1024.0f/32768.0f*board::RadPerDegree);
  }
  rawAcceleration={values[0],values[1],values[2]};
  acceleration=board::toBody(rawAcceleration);
  gyro=board::toBody({values[3],values[4],values[5]});
  // Saturated readings invalidate attitude instead of integrating clipped rates.
  for(int i=0;i<3;++i)if(std::fabs(values[i])>3.95f)return SampleResult::Invalid;
  for(int i=3;i<6;++i)if(std::fabs(values[i])>17.7f)return SampleResult::Invalid;
  return SampleResult::Valid;
}

void samplingTask(void*) {
  instrument::AttitudeFilter filter;
  instrument::StationaryCalibration calibration;
  Vec3 bias{0,0,0};
  Snapshot state;
  bool initialized=false;
  uint32_t lastAttempt=millis()-1000,previousMicros=0;
  TickType_t wake=xTaskGetTickCount();
  for(;;){
    uint32_t command;
    if(xQueueReceive(commands,&command,0)==pdTRUE){
      state.calibrationId=command;
      state.calibrated=false;
      state.calibrationCount=0;
      calibration.reset();
      previousMicros=0;
    }
    if(!initialized&&static_cast<uint32_t>(millis()-lastAttempt)>=1000){
      lastAttempt=millis();
      initialized=initializeImu();
      state.present=initialized;
      state.calibrated=false;
      calibration.reset();
      state.calibrationCount=0;
      previousMicros=0;
      state.sampledAt=0;
    }
    Vec3 acceleration,gyro,raw;
    const SampleResult result=initialized?readSample(acceleration,gyro,raw):SampleResult::NotReady;
    if(result==SampleResult::Invalid){
      // Known I2C faults or clipped motion invalidate immediately, even when
      // the last good sample is recent. Never integrate across missing motion.
      state.calibrated=false;
      state.sampleFault=true;
      state.calibrationCount=0;
      calibration.reset();
      previousMicros=0;
      const uint32_t age=state.sampledAt?static_cast<uint32_t>(millis()-state.sampledAt):static_cast<uint32_t>(millis()-lastAttempt);
      if(age>500){initialized=false;state.present=false;}
    }else if(result==SampleResult::Valid){
      state.sampleFault=false;
      const uint32_t now=micros();
      const float dt=previousMicros?static_cast<uint32_t>(now-previousMicros)*1e-6f:0;
      previousMicros=now;
      state.sampledAt=millis();
      ++state.samples;
      state.rawAcceleration=raw;
      state.gyroDps={gyro.x/board::RadPerDegree,gyro.y/board::RadPerDegree,gyro.z/board::RadPerDegree};
      if(dt>0.1f){
        state.calibrated=false;
        calibration.reset();
      }
      if(!state.calibrated){
        if(calibration.add(acceleration,gyro)){
          bias=calibration.bias();
          filter.reset(calibration.acceleration());
          filter.zeroReference();
          state.calibrated=true;
        }
        state.calibrationCount=calibration.count();
      }else if(!filter.update(acceleration,{gyro.x-bias.x,gyro.y-bias.y,gyro.z-bias.z},dt)){
        state.calibrated=false;
        calibration.reset();
        state.calibrationCount=0;
      }
      state.pitch=filter.pitch();
      state.roll=filter.roll();
    }else if(initialized){
      const uint32_t age=state.sampledAt?static_cast<uint32_t>(millis()-state.sampledAt):static_cast<uint32_t>(millis()-lastAttempt);
      if(age>100){calibration.reset();state.calibrationCount=0;state.calibrated=false;}
      if(age>500){initialized=false;state.present=false;}
    }
    xQueueOverwrite(snapshots,&state);
    pollTouch(millis());
    pollPowerButton(millis());
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
    const uint32_t command=calibrationSession.request();
    if(!command)return;
    xQueueOverwrite(commands,&command);
    Serial.printf("CALIBRATION start id=%lu; keep installation level and still\n",(unsigned long)command);
  }
  displayPipeline.invalidate();
}

void controls() {
  const uint32_t now=millis();
  int8_t direction;
  if(xQueueReceive(swipes,&direction,0)==pdTRUE){
    applyControl(instrumentControls.step(direction));swipeCount.fetch_add(1);
    Serial.printf("SWIPE %s -> %s\n",direction>0?"up":"down",instrumentControls.airspeed()?"airspeed":instrumentControls.attitude()?"attitude":instrumentControls.altimeter()?"altimeter":"hud");
  }
  if(calibrationClick.exchange(false,std::memory_order_acquire))applyControl(instrumentControls.shortPress());
  // Bound command processing so a continuously streaming host cannot starve drawing.
  for(int count=0;count<64&&Serial.available();++count)
    applyControl(instrumentControls.input(static_cast<char>(Serial.read()),now));
}

void setup() {
  // HWCDC defaults to 256 bytes; complete per-frame diagnostics can exceed that.
  // Allocate once at startup while retaining zero-wait writes during rendering.
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);
  instrument::configureDiagnostics(Serial);
  Serial.printf("CPU MHz: %u; band rows: %u; DMA chunk pixels: %u\n",getCpuFrequencyMhz(),instrument::DisplayBandRows,static_cast<unsigned>(fastPanel.chunkPixels()));
  pinMode(board::Boot,INPUT_PULLUP);
  Wire.begin(board::Sda,board::Scl,400000);
  Wire.setTimeOut(20);
  // Preserve the factory rail voltages and charging configuration.
  const bool pmuPresent=pmu.init(Wire,board::Sda,board::Scl);
  Serial.printf("AXP2101: %s\n",pmuPresent?"present":"not found");
  if(pmuPresent)pmu.disableLongPressShutdown();
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
  if(!renderer.begin())stopWithMessage("NO FRAME MEMORY");
  if(!airspeedRenderer.begin())stopWithMessage("NO AIRSPEED MEMORY");
  if(!altimeterRenderer.begin())stopWithMessage("NO ALTIMETER MEMORY");
  if(!hudRenderer.begin())stopWithMessage("NO HUD MEMORY");
  initializeTouch();
  snapshots=xQueueCreate(1,sizeof(Snapshot));
  commands=xQueueCreate(1,sizeof(uint32_t));
  swipes=xQueueCreate(4,sizeof(int8_t));
  if(!snapshots||!commands||!swipes)stopWithMessage("NO QUEUE MEMORY");
  if(xTaskCreatePinnedToCore(samplingTask,"attitude",6144,nullptr,3,nullptr,0)!=pdPASS)
    stopWithMessage("IMU TASK FAILED");
  if(!displayPipeline.begin(display,fastPanel,renderer,airspeedRenderer,altimeterRenderer,hudRenderer,frames,snapshots))stopWithMessage("DISPLAY TASK FAILED");
  Serial.println("AIRSPEED SIM. PWR click: shared calibration; BOOT/long press: no runtime action; swipe: switch; d: demo. a: airspeed; h: attitude; c: calibrate; v135 + newline: simulated km/h; t: altimeter; u: HUD (IMU attitude, simulated IAS/ALT/HDG); b275 + newline: simulated meters (0-3000).");
}

void loop() {
  controls();
  instrumentControls.update(millis());
  Snapshot state;
  xQueuePeek(snapshots,&state,0);
  if(calibrationSession.observe(state.calibrationId,state.calibrated)){
    displayPipeline.invalidate();
    Serial.printf("CALIBRATION complete id=%lu; shared pitch=%.2f roll=%.2f\n",(unsigned long)state.calibrationId,state.pitch,state.roll);
  }
  const bool calibrating=calibrationSession.active();
  const bool airspeed=!calibrating&&instrumentControls.airspeed(),altimeter=!calibrating&&instrumentControls.altimeter(),hud=!calibrating&&instrumentControls.hud(),demo=!calibrating&&instrumentControls.demo();
  const bool fresh=state.sampledAt&&static_cast<uint32_t>(millis()-state.sampledAt)<=100;
  const bool attitude=!calibrating&&instrumentControls.attitude()&&(demo||(state.calibrated&&fresh));
  instrument::DisplayStats stats=displayPipeline.stats();
  static uint32_t sequence=0;
  ++sequence;
  const uint32_t generation=displayPipeline.generation();
  float pitch=state.pitch,roll=state.roll;
  if(instrumentControls.attitude()&&demo){float t=static_cast<uint32_t>(millis()-instrumentControls.demoStarted())*.001f;pitch=20*std::sin(t*.5f);roll=40*std::sin(t*.31f);}
  fpsOverlay.update(stats.fps);
  instrument::DisplayJob request;
  request.state=state;request.demo=demo;request.generation=generation;
  request.sequence=sequence;request.attitude=attitude;
  request.altimeter=altimeter;request.altitudeM=instrumentControls.altitude();
  request.airspeed=airspeed;request.speedKmh=instrumentControls.speed();
  request.hud=hud;
  if(hud){
    auto& h=request.hudInput;h.nowMs=millis();
    h.speedKmh=instrumentControls.speed();h.altitudeM=instrumentControls.altitude();h.climb=instrumentControls.climb();
    h.pitch=state.pitch;h.roll=state.roll;
    h.attitudeValid=state.calibrated&&fresh&&!state.sampleFault;
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
    const int y=job.firstRow;
    if(job.altimeter||job.hud){
      size_t offset=0;uint32_t backgroundUs=0,foregroundUs=0;
      for(int i=0;i<job.altitudeDamage.count;++i){
        const auto r=job.altitudeDamage.regions[i];
        if(job.hud?!instrument::HudRenderer::backgroundRegion(i):bool(i&1)){
          uint32_t tick=micros();
          if(job.hud){
            auto* target=displayPipeline.airspeedPixels(slot)+offset;
            const size_t pixels=size_t(r.width)*r.height;
            auto* scratch=pixels<=466*instrument::DisplayBandRows/2?frames[slot]+466*instrument::DisplayBandRows/2:target;
            hudRenderer.renderPrepared(scratch,slot,r);
            if(scratch!=target)std::memcpy(target,scratch,pixels*sizeof(uint16_t));
          }
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
    }else if(attitude){
      uint32_t began=micros();
      renderer.renderForegroundBand(frames[slot],pitch,roll,y,job.rows,true);
      const instrument::CircleBand band=instrument::circleBandBounds(y,job.rows);
      fpsOverlay.blitBand(frames[slot],band.width,y,job.rows,band.x);
      if(demo)fpsOverlay.blitDemoBand(frames[slot],band.width,y,job.rows,band.x);
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
    char message[720];
    const uint32_t tp=touchPosition.load(std::memory_order_relaxed);
    const int length=snprintf(message,sizeof(message),"fps=%.1f pitch=%.1f roll=%.1f valid=%d cal=%u demo=%d raw_g=%.3f,%.3f,%.3f render_ms=%.1f tx_ms=%.1f frame_ms=%.1f max_gap_ms=%.1f sample_hz=%.1f heap=%u drops=%lu setup_ms=%.1f stack=%u,%u,%u stages=%lu,%lu,%lu,%lu mode=%s speed=%.1f altitude_m=%.1f te_us=%lu te_miss=%lu te_pulses=%lu patch=%lu touch=%lu touch_err=%lu touch_xy=%u,%u touch_n=%u swipes=%lu scan_us=%ld,%ld cal_id=%lu cal_ui=%d pwr_clicks=%lu\n",
      stats.fps,hud?request.hudInput.pitch:state.pitch,hud?request.hudInput.roll:state.roll,state.calibrated&&fresh,state.calibrationCount,demo,
      state.rawAcceleration.x,state.rawAcceleration.y,state.rawAcceleration.z,
      stats.renderUs/1000.f,stats.transferUs/1000.f,stats.intervalUs/1000.f,stats.maxGapUs/1000.f,
      sampleHz,ESP.getFreeHeap(),static_cast<unsigned long>(droppedLogs),stats.setupUs/1000.f,
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),stats.stackFree,stats.backgroundStackFree,
      (unsigned long)stats.stages[0],(unsigned long)stats.stages[1],(unsigned long)stats.stages[2],(unsigned long)stats.stages[3],airspeed?"airspeed":altimeter?"altimeter":hud?"hud":"attitude",hud?request.hudInput.speedKmh:instrumentControls.speed(),hud?request.hudInput.altitudeM:instrumentControls.altitude(),(unsigned long)stats.tePeriodUs,(unsigned long)stats.teMisses,(unsigned long)stats.tePulses,(unsigned long)stats.patchPixels,(unsigned long)touchPackets.load(),(unsigned long)touchErrors.load(),unsigned((tp>>9)&511),unsigned(tp&511),unsigned(tp>>18),(unsigned long)swipeCount.load(),(long)stats.scanMinUs,(long)stats.scanMaxUs,(unsigned long)state.calibrationId,calibrating,(unsigned long)calibrationClicks.load());
    if(length<=0||length>=static_cast<int>(sizeof(message))||!instrument::tryDiagnostic(Serial,message,length))++droppedLogs;
  }
  // Allow idle/housekeeping regularly without inserting a pause in every image.
  if((sequence&3u)==0)delay(1);
}

// CPU1 now also prepares HUD geometry and dirty-region matching.
SET_LOOP_TASK_STACK_SIZE(12288);
