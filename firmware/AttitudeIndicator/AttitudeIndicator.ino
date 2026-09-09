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
#include "board_config.h"
#include "attitude.h"
#include "renderer.h"
#include "diagnostics.h"
#include "fps_overlay.h"
#include "fast_panel.h"
#include "display_pipeline.h"
#include "circle_band.h"

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
instrument::FastPanel fastPanel;
instrument::DisplayPipeline displayPipeline;
instrument::FpsOverlay fpsOverlay(true);
uint16_t* frames[instrument::DisplayBufferCount]={};
QueueHandle_t snapshots=nullptr,commands=nullptr;
bool demo=false;
uint32_t demoStarted=0;
std::atomic<bool> panelReady{false};

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
    uint8_t command;
    if(xQueueReceive(commands,&command,0)==pdTRUE){
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

void toggleDemo() { demo=!demo;demoStarted=millis();displayPipeline.invalidate(); }
void requestCalibration() {
  demo=false;
  displayPipeline.invalidate();
  const uint8_t command=1;
  xQueueOverwrite(commands,&command);
}

void controls() {
  static bool down=false,longHandled=false;
  static uint32_t pressedAt=0;
  const bool pressed=digitalRead(board::Boot)==LOW;
  const uint32_t now=millis();
  if(pressed&&!down){pressedAt=now;longHandled=false;}
  if(pressed&&!longHandled&&static_cast<uint32_t>(now-pressedAt)>=1500){toggleDemo();longHandled=true;}
  if(!pressed&&down&&!longHandled&&static_cast<uint32_t>(now-pressedAt)>=50)requestCalibration();
  down=pressed;
  while(Serial.available()){
    int command=Serial.read();
    if(command=='c')requestCalibration();
    if(command=='d')toggleDemo();
  }
}

void setup() {
  Serial.begin(115200);
  instrument::configureDiagnostics(Serial);
  Serial.printf("CPU MHz: %u; band rows: %u; DMA chunk pixels: %u\n",getCpuFrequencyMhz(),instrument::DisplayBandRows,static_cast<unsigned>(fastPanel.chunkPixels()));
  pinMode(board::Boot,INPUT_PULLUP);
  Wire.begin(board::Sda,board::Scl,400000);
  Wire.setTimeOut(20);
  // Preserve the factory rail voltages and charging configuration.
  const bool pmuPresent=pmu.init(Wire,board::Sda,board::Scl);
  Serial.printf("AXP2101: %s\n",pmuPresent?"present":"not found");
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
  snapshots=xQueueCreate(1,sizeof(Snapshot));
  commands=xQueueCreate(1,sizeof(uint8_t));
  if(!snapshots||!commands)stopWithMessage("NO QUEUE MEMORY");
  if(xTaskCreatePinnedToCore(samplingTask,"attitude",6144,nullptr,3,nullptr,0)!=pdPASS)
    stopWithMessage("IMU TASK FAILED");
  if(!displayPipeline.begin(display,fastPanel,renderer,frames,snapshots))stopWithMessage("DISPLAY TASK FAILED");
  Serial.println("Keep model level and still. BOOT short / c: calibrate; hold 1.5s / d: demo.");
}

void loop() {
  controls();
  Snapshot state;
  xQueuePeek(snapshots,&state,0);
  const bool fresh=state.sampledAt&&static_cast<uint32_t>(millis()-state.sampledAt)<=100;
  const bool attitude=demo||(state.calibrated&&fresh);
  instrument::DisplayStats stats=displayPipeline.stats();
  static uint32_t sequence=0;
  ++sequence;
  const uint32_t generation=displayPipeline.generation();
  float pitch=state.pitch,roll=state.roll;
  if(demo){float t=static_cast<uint32_t>(millis()-demoStarted)*.001f;pitch=20*std::sin(t*.5f);roll=40*std::sin(t*.31f);}
  fpsOverlay.update(stats.fps);
  instrument::DisplayJob request;
  request.state=state;request.demo=demo;request.generation=generation;
  request.sequence=sequence;request.attitude=attitude;
  displayPipeline.startFrame(request,pitch,roll);
  for(int y=0;y<(attitude?466:1);y+=instrument::DisplayBandRows){
    instrument::DisplayJob job=displayPipeline.nextBand();
    const unsigned slot=job.slot;
    if(attitude){
      uint32_t began=micros();
      renderer.renderForegroundBand(frames[slot],pitch,roll,y,job.rows,true);
      const instrument::CircleBand band=instrument::circleBandBounds(y,job.rows);
      fpsOverlay.blitBand(frames[slot],band.width,y,job.rows,band.x);
      if(demo)fpsOverlay.blitDemoBand(frames[slot],band.width,y,job.rows,band.x);
      job.renderUs+=micros()-began;
      for(unsigned i=1;i<4;++i)job.stages[i]=instrument::renderStages[i];
    }
    displayPipeline.submit(job);
  }
  static uint32_t lastLog=0,lastSamples=0,droppedLogs=0;
  if(static_cast<uint32_t>(millis()-lastLog)>=1000){
    const float sampleHz=(state.samples-lastSamples)*1000.f/(millis()-lastLog);
    lastSamples=state.samples;lastLog=millis();
    char message[480];
    const int length=snprintf(message,sizeof(message),"fps=%.1f pitch=%.1f roll=%.1f valid=%d cal=%u demo=%d raw_g=%.3f,%.3f,%.3f render_ms=%.1f tx_ms=%.1f frame_ms=%.1f max_gap_ms=%.1f sample_hz=%.1f heap=%u drops=%lu setup_ms=%.1f stack=%u,%u,%u stages=%lu,%lu,%lu,%lu\n",
      stats.fps,state.pitch,state.roll,state.calibrated&&fresh,state.calibrationCount,demo,
      state.rawAcceleration.x,state.rawAcceleration.y,state.rawAcceleration.z,
      stats.renderUs/1000.f,stats.transferUs/1000.f,stats.intervalUs/1000.f,stats.maxGapUs/1000.f,
      sampleHz,ESP.getFreeHeap(),static_cast<unsigned long>(droppedLogs),stats.setupUs/1000.f,
      static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)),stats.stackFree,stats.backgroundStackFree,
      (unsigned long)stats.stages[0],(unsigned long)stats.stages[1],(unsigned long)stats.stages[2],(unsigned long)stats.stages[3]);
    if(length<=0||length>=static_cast<int>(sizeof(message))||!instrument::tryDiagnostic(Serial,message,length))++droppedLogs;
  }
  // Allow idle/housekeeping regularly without inserting a pause in every image.
  if((sequence&3u)==0)delay(1);
}
