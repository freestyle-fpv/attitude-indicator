#pragma once
#include "runtime_state.h"
#include "fast_panel.h"
#include "fps_overlay.h"
#include "renderer.h"
#include "airspeed_renderer.h"
#include "altimeter_renderer.h"
#include "hud_renderer.h"
#include "panel_sync.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
namespace instrument {
constexpr int DisplayBandRows=60;
constexpr unsigned DisplayBufferCount=3;
struct DisplayJob {
  unsigned slot=0;
  uint32_t generation=0,sequence=0,renderUs=0,stages[4]={};
  unsigned backgroundStackFree=0;
  int firstRow=0,rows=0;
  bool lastBand=true;
  Snapshot state;
  bool demo=false,attitude=false,airspeed=false,altimeter=false,hud=false;
  float speedKmh=0,altitudeM=0;
  HudInput hudInput;
  AltimeterDamage altitudeDamage;
  AirspeedRect patch={0,0,466,466};
  bool fullAirspeed=false;
};
struct DisplayStats {
  float fps=-1;
  int32_t scanMinUs=0,scanMaxUs=0;
  uint32_t renderUs=0,transferUs=0,intervalUs=0,maxGapUs=0,setupUs=0,stages[4]={};
  uint32_t tePeriodUs=0,teMisses=0,tePulses=0,patchPixels=0;
  unsigned stackFree=0;
  unsigned backgroundStackFree=0;
};
class DisplayPipeline {
 public:
  bool begin(PanelDisplay& display,FastPanel& panel,Renderer& renderer,AirspeedRenderer& airspeed,AltimeterRenderer& altimeter,HudRenderer& hud,uint16_t** frames,QueueHandle_t snapshots);
  void startFrame(const DisplayJob& job,float pitch,float roll);
  DisplayJob nextBand(){DisplayJob job;xQueueReceive(ready_,&job,portMAX_DELAY);return job;}
  DisplayJob parallelBackgroundDone(){DisplayJob job;xQueueReceive(parallelDone_,&job,portMAX_DELAY);return job;}
  void submit(const DisplayJob& job){xQueueSend(jobs_,&job,portMAX_DELAY);}
  uint16_t* airspeedPixels(unsigned slot){return airFrames_[slot];}
  DisplayStats stats(){DisplayStats s;xQueuePeek(stats_,&s,0);return s;}
  void invalidate(){generation_.fetch_add(1,std::memory_order_relaxed);}
  uint32_t generation()const{return generation_.load(std::memory_order_relaxed);}
 private:
  static void entry(void* self){static_cast<DisplayPipeline*>(self)->run();}
  static void backgroundEntry(void* self){static_cast<DisplayPipeline*>(self)->runBackground();}
  struct RenderRequest { DisplayJob job;float pitch,roll; };
  void runBackground();
  void run();
  void presentDial(const DisplayJob&,DisplayStats&);
  void text(const char* message,int y,int size,uint16_t color);
  void status(const Snapshot& state);
  PanelDisplay* display_=nullptr;
  FastPanel* panel_=nullptr;
  Renderer* renderer_=nullptr;
  AirspeedRenderer* airspeed_=nullptr;
  AltimeterRenderer* altimeter_=nullptr;
  HudRenderer* hud_=nullptr;
  uint16_t** frames_=nullptr;
  QueueHandle_t free_=nullptr,jobs_=nullptr,stats_=nullptr,snapshots_=nullptr;
  QueueHandle_t requests_=nullptr,ready_=nullptr,parallelDone_=nullptr;
  std::atomic<uint32_t> generation_{0};
  PanelSync sync_;
  uint16_t* airFrames_[DisplayBufferCount]={};
  FpsOverlay* airOverlay_=nullptr;
  int airFpsTenths_=-999;
  FpsCounter fps_;
  FpsOverlay overlay_;
};
}
