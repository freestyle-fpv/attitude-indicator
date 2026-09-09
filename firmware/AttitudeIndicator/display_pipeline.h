#pragma once
#include "runtime_state.h"
#include "fast_panel.h"
#include "fps_overlay.h"
#include "renderer.h"
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
  bool demo=false,attitude=false;
};
struct DisplayStats {
  float fps=-1;
  uint32_t renderUs=0,transferUs=0,intervalUs=0,maxGapUs=0,setupUs=0,stages[4]={};
  unsigned stackFree=0;
  unsigned backgroundStackFree=0;
};
class DisplayPipeline {
 public:
  bool begin(PanelDisplay& display,FastPanel& panel,Renderer& renderer,uint16_t** frames,QueueHandle_t snapshots);
  void startFrame(const DisplayJob& job,float pitch,float roll){RenderRequest r={job,pitch,roll};xQueueSend(requests_,&r,portMAX_DELAY);}
  DisplayJob nextBand(){DisplayJob job;xQueueReceive(ready_,&job,portMAX_DELAY);return job;}
  void submit(const DisplayJob& job){xQueueSend(jobs_,&job,portMAX_DELAY);}
  DisplayStats stats(){DisplayStats s;xQueuePeek(stats_,&s,0);return s;}
  void invalidate(){generation_.fetch_add(1,std::memory_order_relaxed);}
  uint32_t generation()const{return generation_.load(std::memory_order_relaxed);}
 private:
  static void entry(void* self){static_cast<DisplayPipeline*>(self)->run();}
  static void backgroundEntry(void* self){static_cast<DisplayPipeline*>(self)->runBackground();}
  struct RenderRequest { DisplayJob job;float pitch,roll; };
  void runBackground();
  void run();
  void text(const char* message,int y,int size,uint16_t color);
  void status(const Snapshot& state);
  PanelDisplay* display_=nullptr;
  FastPanel* panel_=nullptr;
  Renderer* renderer_=nullptr;
  uint16_t** frames_=nullptr;
  QueueHandle_t free_=nullptr,jobs_=nullptr,stats_=nullptr,snapshots_=nullptr;
  QueueHandle_t requests_=nullptr,ready_=nullptr;
  std::atomic<uint32_t> generation_{0};
  FpsCounter fps_;
  FpsOverlay overlay_;
};
}
