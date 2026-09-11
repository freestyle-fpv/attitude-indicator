#include "display_pipeline.h"
#include "display_policy.h"
#include "board_config.h"
#include <esp_heap_caps.h>
#include <new>
#include <cstring>
#include <initializer_list>
namespace instrument {
bool DisplayPipeline::begin(PanelDisplay& display,FastPanel& panel,Renderer& renderer,AirspeedRenderer& airspeed,AltimeterRenderer& altimeter,HudRenderer& hud,uint16_t** frames,QueueHandle_t snapshots){
  display_=&display;panel_=&panel;renderer_=&renderer;airspeed_=&airspeed;altimeter_=&altimeter;hud_=&hud;frames_=frames;snapshots_=snapshots;
  for(auto& frame:airFrames_)frame=static_cast<uint16_t*>(heap_caps_malloc(466*466*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  void* overlayMemory=heap_caps_malloc(sizeof(FpsOverlay),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(overlayMemory)airOverlay_=new(overlayMemory) FpsOverlay(true);
  free_=xQueueCreate(DisplayBufferCount,sizeof(unsigned));jobs_=xQueueCreate(1,sizeof(DisplayJob));stats_=xQueueCreate(1,sizeof(DisplayStats));
  requests_=xQueueCreate(1,sizeof(RenderRequest));ready_=xQueueCreate(1,sizeof(DisplayJob));parallelDone_=xQueueCreate(1,sizeof(DisplayJob));
  auto cleanup=[this](){
    for(auto q:{free_,jobs_,stats_,requests_,ready_,parallelDone_})if(q)vQueueDelete(q);
    free_=jobs_=stats_=requests_=ready_=parallelDone_=nullptr;
    if(airOverlay_){airOverlay_->~FpsOverlay();std::free(airOverlay_);airOverlay_=nullptr;}
    for(auto& frame:airFrames_){std::free(frame);frame=nullptr;}
  };
  if(!airFrames_[0]||!airFrames_[1]||!airFrames_[2]||!airOverlay_||!free_||!jobs_||!stats_||!requests_||!ready_||!parallelDone_){cleanup();return false;}
  for(unsigned i=0;i<DisplayBufferCount;++i)xQueueSend(free_,&i,0);
  TaskHandle_t background=nullptr;
  if(xTaskCreatePinnedToCore(backgroundEntry,"background",8192,this,1,&background,0)!=pdPASS){cleanup();return false;}
  // DMA submission preempts background work; the 200 Hz IMU is priority 3.
  if(xTaskCreatePinnedToCore(entry,"display",8192,this,2,nullptr,0)!=pdPASS){
    vTaskDelete(background);cleanup();return false;
  }
  return true;
}
void DisplayPipeline::startFrame(const DisplayJob& job,float pitch,float roll){
  RenderRequest request={job,pitch,roll};
  if(job.hud){
    // The caller is CPU1. Prepare the next HUD while CPU0 is submitting the
    // previous frame, rather than waiting for its display task to yield.
    xQueueReceive(free_,&request.job.slot,portMAX_DELAY);
    const uint32_t began=micros();hud_->prepareFrame(request.job.slot,job.hudInput);
    request.job.stages[0]=micros()-began;
  }
  xQueueSend(requests_,&request,portMAX_DELAY);
}
void DisplayPipeline::runBackground(){
  bool wasAirspeed=false,wasAltimeter=false,wasHud=false;float previousSpeed=0,previousAltitude=0;uint32_t previousGeneration=0;
  for(;;){
    RenderRequest request;xQueueReceive(requests_,&request,portMAX_DELAY);
    const bool image=request.job.attitude;
    for(int y=0;y<(image?466:1);y+=DisplayBandRows){
      DisplayJob job=request.job;
      // HUD already owns a prepared slot. Other modes acquire it here.
      // Every job still traverses both FIFO stages; only display releases it.
      if(!job.hud)xQueueReceive(free_,&job.slot,portMAX_DELAY);
      job.firstRow=y;job.rows=std::min(DisplayBandRows,466-y);
      job.lastBand=!image||y+job.rows==466;
      if(job.altimeter||job.hud){
        const uint32_t began=micros();
        job.fullAirspeed=!(job.hud?wasHud:wasAltimeter)||job.generation!=previousGeneration;
        if(job.fullAirspeed){job.altitudeDamage.count=1;job.altitudeDamage.regions[0]={0,0,466,466};}
        else job.altitudeDamage=job.hud?hud_->damagePrepared(job.slot):AltimeterRenderer::damage(previousAltitude,job.altitudeM);
        // Publish the immutable pose before either core draws. Each core owns
        // alternating regions, including background copies, in the same slot.
        if(!job.hud)altimeter_->prepareFrame(job.slot,job.altitudeM);
        job.stages[1]=(job.hud?job.stages[0]:0)+micros()-began;job.renderUs=job.stages[1];
        xQueueSend(ready_,&job,portMAX_DELAY);
        size_t offset=0;
        for(int i=0;i<job.altitudeDamage.count;++i){
          const auto r=job.altitudeDamage.regions[i];
          if(job.hud?HudRenderer::backgroundRegion(i):(i&1)==0){
            uint32_t tick=micros();
            if(job.hud){
              // This slot's DMA memory is idle until both raster workers finish.
              // Each worker owns half; render locally then copy sequentially to PSRAM.
              auto* target=airFrames_[job.slot]+offset;
              const size_t pixels=size_t(r.width)*r.height;
              auto* scratch=pixels<=466*DisplayBandRows/2?frames_[job.slot]:target;
              hud_->renderPrepared(scratch,job.slot,r);
              if(scratch!=target)std::memcpy(target,scratch,pixels*sizeof(uint16_t));
            }
            else {altimeter_->renderBackgroundRectangle(airFrames_[job.slot]+offset,r);job.stages[0]+=micros()-tick;
              tick=micros();altimeter_->renderForegroundPrepared(airFrames_[job.slot]+offset,job.slot,r);}
            job.stages[1]+=micros()-tick;
          }
          offset+=r.width*r.height;
        }
        job.renderUs=(job.hud?0:job.stages[0])+job.stages[1];
        if(job.hud)job.stages[2]=job.stages[1]-job.stages[0];
      }else if(job.airspeed){
        const uint32_t began=micros();
        job.fullAirspeed=!wasAirspeed||job.generation!=previousGeneration;
        job.patch=job.fullAirspeed?AirspeedRect{0,0,466,466}:AirspeedRenderer::damage(previousSpeed,job.speedKmh);
        airspeed_->renderBackgroundRectangle(airFrames_[job.slot],job.patch);
        job.renderUs=job.stages[0]=micros()-began;
      }else if(image){
        const uint32_t began=micros();
        renderer_->renderBackgroundBand(frames_[job.slot],request.pitch,request.roll,y,job.rows,true);
        job.renderUs=job.stages[0]=micros()-began;
      }
      job.backgroundStackFree=uxTaskGetStackHighWaterMark(nullptr);
      if(job.altimeter||job.hud)xQueueSend(parallelDone_,&job,portMAX_DELAY);
      else xQueueSend(ready_,&job,portMAX_DELAY);
    }
    wasAirspeed=request.job.airspeed;wasAltimeter=request.job.altimeter;wasHud=request.job.hud;previousSpeed=request.job.speedKmh;previousAltitude=request.job.altitudeM;previousGeneration=request.job.generation;
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
void DisplayPipeline::presentDial(const DisplayJob& job,DisplayStats& stats){
  const AirspeedRect rect=job.patch;
  uint16_t* pixels=airFrames_[job.slot];
  airOverlay_->update(fps_.value());
  const int count=(job.altimeter||job.hud)?job.altitudeDamage.count:1;
  size_t offset=0;
  for(int i=0;i<count;++i){
    const auto r=(job.altimeter||job.hud)?job.altitudeDamage.regions[i]:rect;
    if(job.hud||job.altimeter||job.fullAirspeed){
      airOverlay_->blitBand(pixels+offset,r.width,r.y,r.height,r.x);
      if(!job.hud)airOverlay_->blitSimBand(pixels+offset,r.width,r.y,r.height,r.x,job.altimeter);
    }
    offset+=r.width*r.height;
  }
  stats.renderUs=job.renderUs;
  for(unsigned i=0;i<4;++i)stats.stages[i]=job.stages[i];
  // Compose the entire moving region before waiting: it is then transmitted
  // from top to bottom. HUD trails the scan; smaller dials start at blank.
  // Static dial pixels stay in GRAM.
  const uint32_t measuredPeriod=sync_.period();
  sync_.wait(job.hud?(measuredPeriod>=12000&&measuredPeriod<=22000?measuredPeriod/4:4000):0);
  const uint32_t edge=sync_.lastEdge(),period=sync_.period();
  const uint32_t transfer=micros();
  offset=0;
  if(job.hud){
    panel_->presentRegions(*display_,pixels,job.altitudeDamage.regions,count,frames_[job.slot],466*DisplayBandRows);
    stats.setupUs+=panel_->setupMicros();for(int i=0;i<count;++i)offset+=job.altitudeDamage.regions[i].width*job.altitudeDamage.regions[i].height;
    stats.scanMinUs=1000000;stats.scanMaxUs=-1000000;
    for(int i=0;i<count;++i){const auto r=job.altitudeDamage.regions[i];const auto& t=panel_->regionTiming(i);
      if(!t.began||!t.ended||r.y>=352||r.y+r.height<=112)continue;
      const int32_t first=int32_t(uint64_t(r.y)*period/466)-int32_t(uint32_t(t.began-edge));
      const int32_t last=int32_t(uint64_t(r.y+r.height)*period/466)-int32_t(uint32_t(t.ended-edge));
      stats.scanMinUs=std::min(stats.scanMinUs,std::min(first,last));stats.scanMaxUs=std::max(stats.scanMaxUs,std::max(first,last));
    }

  }else for(int i=0;i<count;++i){
    const auto r=job.altimeter?job.altitudeDamage.regions[i]:rect;
    panel_->presentRect(*display_,pixels+offset,r.x,r.y,r.width,r.height,frames_[job.slot],466*DisplayBandRows);
    stats.setupUs+=panel_->setupMicros();offset+=r.width*r.height;
  }
  stats.patchPixels=offset;
  const int tenths=static_cast<int>(std::max(-1.f,fps_.value())*10+.5f);
  if(!job.hud&&!job.fullAirspeed&&tenths!=airFpsTenths_){
    // The badge begins at odd X=181. Restore its extra border pixel and send
    // the enclosing even-aligned window, keeping the badge visually centered.
    const AirspeedRect badge={FpsOverlay::X-1,FpsOverlay::Y,FpsOverlay::Width+2,FpsOverlay::Height};
    if(job.altimeter)altimeter_->renderBackgroundRectangle(pixels,badge);
    else airspeed_->renderBackgroundRectangle(pixels,badge);
    airOverlay_->blitBand(pixels,badge.width,badge.y,badge.height,badge.x);
    panel_->presentRect(*display_,pixels,badge.x,badge.y,badge.width,badge.height,frames_[job.slot],466*DisplayBandRows);
    stats.setupUs+=panel_->setupMicros();
  }
  airFpsTenths_=tenths;
  stats.transferUs=micros()-transfer;
  stats.tePeriodUs=sync_.period();stats.tePulses=sync_.pulses();stats.teMisses=sync_.misses();
  stats.backgroundStackFree=job.backgroundStackFree;
}
void DisplayPipeline::run(){
  sync_.begin(board::LcdTe);panel_->enableTearingSignal();
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
    const FrameDecision decision=job.hud?decideHudFrame(job.demo,current,jobFresh,job.hudInput.attitudeValid):
      decideFrame(job.airspeed||job.altimeter,job.attitude,job.demo,current,jobFresh);
    if(decision==FrameDecision::Discard){
      // A discarded first HUD frame never cleared the previous instrument.
      // Force its successor to rebuild the entire screen, not only HUD bands.
      if(job.hud)invalidate();
      discard=true;discardSequence=job.sequence;
      xQueueSend(free_,&job.slot,portMAX_DELAY);vTaskDelay(1);continue;
    }
    const uint32_t began=micros();
    bool completedFrame=false,statusFrame=false;
    if(decision==FrameDecision::Draw&&(job.airspeed||job.altimeter||job.hud)){
      presentDial(job,frameStats);
      completedFrame=true;
    }else if(decision==FrameDecision::Draw){
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
    if(!job.airspeed&&!job.altimeter&&!job.hud)frameStats.transferUs+=completed-began;
    if(completedFrame){
      const uint32_t maxGap=stats.maxGapUs;
      stats=frameStats;stats.maxGapUs=maxGap;
      stats.intervalUs=lastCompleted?static_cast<uint32_t>(completed-lastCompleted):0;
      if(lastCompleted&&millis()>6000&&stats.intervalUs>stats.maxGapUs)stats.maxGapUs=stats.intervalUs;
      lastCompleted=completed;
      // Count complete instrument updates: all attitude bands or one airspeed patch.
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
