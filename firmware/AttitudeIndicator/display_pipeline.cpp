#include "display_pipeline.h"
#include <cstring>
#include <initializer_list>
namespace instrument {
bool DisplayPipeline::begin(PanelDisplay& display,FastPanel& panel,Renderer& renderer,uint16_t** frames,QueueHandle_t snapshots){
  display_=&display;panel_=&panel;renderer_=&renderer;frames_=frames;snapshots_=snapshots;
  free_=xQueueCreate(DisplayBufferCount,sizeof(unsigned));jobs_=xQueueCreate(1,sizeof(DisplayJob));stats_=xQueueCreate(1,sizeof(DisplayStats));
  requests_=xQueueCreate(1,sizeof(RenderRequest));ready_=xQueueCreate(1,sizeof(DisplayJob));
  auto cleanup=[this](){
    for(auto q:{free_,jobs_,stats_,requests_,ready_})if(q)vQueueDelete(q);
    free_=jobs_=stats_=requests_=ready_=nullptr;
  };
  if(!free_||!jobs_||!stats_||!requests_||!ready_){cleanup();return false;}
  for(unsigned i=0;i<DisplayBufferCount;++i)xQueueSend(free_,&i,0);
  TaskHandle_t background=nullptr;
  if(xTaskCreatePinnedToCore(backgroundEntry,"background",6144,this,1,&background,0)!=pdPASS){cleanup();return false;}
  // DMA submission preempts background work; the 200 Hz IMU is priority 3.
  if(xTaskCreatePinnedToCore(entry,"display",8192,this,2,nullptr,0)!=pdPASS){
    vTaskDelete(background);cleanup();return false;
  }
  return true;
}
void DisplayPipeline::runBackground(){
  for(;;){
    RenderRequest request;xQueueReceive(requests_,&request,portMAX_DELAY);
    for(int y=0;y<(request.job.attitude?466:1);y+=DisplayBandRows){
      DisplayJob job=request.job;
      // Only this stage acquires a free slot. Every job, including stale work,
      // traverses both FIFO stages; only the display stage returns its slot.
      xQueueReceive(free_,&job.slot,portMAX_DELAY);
      job.firstRow=y;job.rows=std::min(DisplayBandRows,466-y);
      job.lastBand=!job.attitude||y+job.rows==466;
      if(job.attitude){
        const uint32_t began=micros();
        renderer_->renderBackgroundBand(frames_[job.slot],request.pitch,request.roll,y,job.rows,true);
        job.renderUs=job.stages[0]=micros()-began;
      }
      job.backgroundStackFree=uxTaskGetStackHighWaterMark(nullptr);
      xQueueSend(ready_,&job,portMAX_DELAY);
    }
    // Bound continuous CPU0 work even when all stages remain ready.
    vTaskDelay(1);
  }
}
void DisplayPipeline::text(const char* message,int y,int size,uint16_t color){
  display_->setTextSize(size);display_->setTextColor(color);
  display_->setCursor((466-static_cast<int>(std::strlen(message))*6*size)/2,y);display_->print(message);
}
void DisplayPipeline::status(const Snapshot& state){
  const bool fresh=state.sampledAt&&static_cast<uint32_t>(millis()-state.sampledAt)<=100;
  display_->fillScreen(RGB565_BLACK);display_->drawCircle(233,233,205,0x39E7);
  if(!state.present||state.sampleFault||(!fresh&&state.sampledAt)){
    text("IMU UNAVAILABLE",198,2,RGB565_RED);text("RETRYING",232,2,0xBDF7);
  }else{
    text("KEEP LEVEL & STILL",184,2,RGB565_WHITE);text("CALIBRATING",220,2,RGB565_YELLOW);
    display_->drawRect(133,260,200,8,0x7BEF);
    display_->fillRect(134,261,198*state.calibrationCount/StationaryCalibration::RequiredSamples,6,RGB565_YELLOW);
  }
  overlay_.update(fps_.value());
  display_->draw16bitRGBBitmap(FpsOverlay::X,FpsOverlay::Y,overlay_.pixels(),FpsOverlay::Width,FpsOverlay::Height);
}
void DisplayPipeline::run(){
  DisplayStats stats,frameStats;
  uint32_t lastCompleted=0,activeSequence=0,discardSequence=0;
  bool discard=false;
  for(;;){
    DisplayJob job;xQueueReceive(jobs_,&job,portMAX_DELAY);
    if(discard&&job.sequence!=discardSequence)discard=false;
    if(job.generation!=generation()||(discard&&job.sequence==discardSequence)){
      xQueueSend(free_,&job.slot,portMAX_DELAY);vTaskDelay(1);continue;
    }
    if(job.sequence!=activeSequence){activeSequence=job.sequence;frameStats={};}
    Snapshot live;xQueuePeek(snapshots_,&live,0);
    const bool current=live.calibrated&&live.sampledAt&&static_cast<uint32_t>(millis()-live.sampledAt)<=100;
    const bool jobFresh=job.state.sampledAt&&static_cast<uint32_t>(millis()-job.state.sampledAt)<=100;
    if(job.attitude&&!job.demo&&current&&!jobFresh){
      discard=true;discardSequence=job.sequence;
      xQueueSend(free_,&job.slot,portMAX_DELAY);vTaskDelay(1);continue;
    }
    const uint32_t began=micros();
    bool completedFrame=false,statusFrame=false;
    if(job.attitude&&(job.demo||current)){
      panel_->presentBand(*display_,frames_[job.slot],job.firstRow,job.rows);
      frameStats.renderUs+=job.renderUs;
      for(unsigned i=0;i<4;++i)frameStats.stages[i]+=job.stages[i];
      frameStats.setupUs+=panel_->setupMicros();
      frameStats.backgroundStackFree=job.backgroundStackFree;
      if(job.lastBand){
        completedFrame=true;
      }
    }else{
      status(live);frameStats={};completedFrame=true;statusFrame=true;
      // A sensor fault mid-frame clears the screen once, then drops all remaining bands.
      discard=true;discardSequence=job.sequence;
    }
    const uint32_t completed=micros();
    frameStats.transferUs+=completed-began;
    if(completedFrame){
      const uint32_t maxGap=stats.maxGapUs;
      stats=frameStats;stats.maxGapUs=maxGap;
      stats.intervalUs=lastCompleted?static_cast<uint32_t>(completed-lastCompleted):0;
      if(lastCompleted&&millis()>6000&&stats.intervalUs>stats.maxGapUs)stats.maxGapUs=stats.intervalUs;
      lastCompleted=completed;
      // One count per COMPLETE 466-row image, never one count per band.
      fps_.frameCompleted(completed);stats.fps=fps_.value();
      stats.stackFree=uxTaskGetStackHighWaterMark(nullptr);xQueueOverwrite(stats_,&stats);
    }
    xQueueSend(free_,&job.slot,portMAX_DELAY);
    // Queued DMA naturally blocks normal bands. Legacy status drawing polls,
    // so explicitly yield there to service IDLE0 and its watchdog.
    if(statusFrame)vTaskDelay(1);
  }
}
}
