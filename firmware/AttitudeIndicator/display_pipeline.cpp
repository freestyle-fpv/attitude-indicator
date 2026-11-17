#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "display_pipeline.h"
#include "display_policy.h"
#include "board_config.h"
#include "circle_band.h"
#include "menu_font.h"
#include <esp_heap_caps.h>
#include <new>
#include <cstring>
#include <initializer_list>
namespace instrument {
namespace {
struct RoundPhase {uint32_t first,last;};
RoundPhase roundPhase(const AltimeterDamage& damage,uint32_t period){
  // QSPI at 40 MHz moves ten RGB565 pixels per microsecond. Bound both ends
  // of each region, allowing measured dispatch/copy overhead and ISR jitter.
  const int minimum=int(period/5);
  // Each window's staging overlaps the previous window's DMA. Budget the
  // extra gaps on the wire here, plus headroom for an IMU or USB interrupt.
  constexpr int lineMargin=600,dispatchUs=300,jitterUs=2200;
  int first=minimum,last=int(period),pixels=0;
  for(int i=0;i<damage.count;++i){
    const auto r=damage.regions[i];
    const int start=int(period*r.y/466),end=int(period*(r.y+r.height)/466);
    first=std::max(first,start+lineMargin-pixels/10);
    last=std::min(last,start+int(period)-lineMargin-jitterUs-(i+1)*dispatchUs-pixels/10);
    pixels+=r.width*r.height;
    first=std::max(first,end+lineMargin-pixels/10);
    last=std::min(last,end+int(period)-lineMargin-jitterUs-(i+1)*dispatchUs-pixels/10);
  }
  if(first>last)return {uint32_t(first),0};
  return {uint32_t(first),uint32_t(last)};
}
// Menu and rotated calibration share the job's existing external buffer.
class JobCanvas : public Arduino_Canvas {
 public:
  JobCanvas(uint16_t* pixels,UsbSide side):Arduino_Canvas(466,466,nullptr,0,0,unsigned(inverse(side))){_framebuffer=pixels;}
  ~JobCanvas(){_framebuffer=nullptr;}
};
void text(Arduino_GFX& canvas,const char* message,int y,int size,uint16_t color,int x=233){
  const int step=menufont::Cell*size;
  x-=static_cast<int>(std::strlen(message)/3)*step/2;
  for(;*message;message+=3,x+=step){
    for(const auto& glyph:menufont::glyphs){
      if(std::memcmp(message,glyph.utf8,3)!=0)continue;
      for(int row=0;row<menufont::Cell;++row){
        uint32_t ink=glyph.rows[row];
        while(ink){
          const int column=__builtin_ctz(ink);ink&=ink-1;
          if(size==1)canvas.drawPixel(x+column,y+row,color);
          else canvas.fillRect(x+column*size,y+row*size,size,size,color);
        }
      }
      break;
    }
  }
}
}
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
  for(auto* frame:airFrames_)if(!frame){cleanup();return false;}
  if(!airOverlay_||!free_||!jobs_||!stats_||!requests_||!ready_||!parallelDone_){cleanup();return false;}
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
    const uint32_t began=micros();hud_->prepareFrame(request.job.slot,job.hudInput,job.options.side,job.options.fps);
    request.job.stages[0]=micros()-began;
  }
  xQueueSend(requests_,&request,portMAX_DELAY);
}
void DisplayPipeline::renderHudRegion(const DisplayJob& job,AirspeedRect r,size_t offset,bool background){
  constexpr size_t half=DisplayHalfBandPixels;
  auto* target=airFrames_[job.slot]+offset;
  const size_t pixels=size_t(r.width)*r.height;
  assert(job.options.side==UsbSide::Left||pixels<=half);
  auto* scratch=pixels<=half?frames_[job.slot]+(background?0:half):target;
  hud_->renderPreparedPanel(scratch,job.slot,r,job.options.side);
  if(scratch!=target)copyPixels(target,scratch,pixels);
}
uint32_t DisplayPipeline::renderRoundRegions(const DisplayJob& job,float pitch,float roll,bool background){
  auto* stages=workerStages[background?0:1];stages[0]=stages[1]=stages[2]=0;
  auto* scratch=frames_[job.slot]+(background?0:DisplayHalfBandPixels);
  const auto& plan=job.altitudeDamage;
  const uint32_t started=micros();
  size_t offset=0;
  unsigned previous=0;
  // Claim one region at a time. CPU1 can take more whenever display DMA
  // preempts the rendering task on CPU0.
  for(unsigned i=roundRegionCursor_.fetch_add(1,std::memory_order_relaxed);i<unsigned(plan.count);
      i=roundRegionCursor_.fetch_add(1,std::memory_order_relaxed)){
    for(;previous<i;++previous)offset+=size_t(plan.regions[previous].width)*plan.regions[previous].height;
    const auto rect=plan.regions[i];
    const size_t pixels=size_t(rect.width)*rect.height;
    assert(pixels<=DisplayHalfBandPixels);
    uint32_t tick=micros();renderer_->renderBackgroundWindow(scratch,pitch,roll,rect);stages[0]+=micros()-tick;
    tick=micros();renderer_->renderPreparedForegroundWindow(scratch,rect);stages[1]+=micros()-tick;
    tick=micros();copyPixels(airFrames_[job.slot]+offset,scratch,pixels);stages[2]+=micros()-tick;
  }
  return micros()-started;
}
uint32_t DisplayPipeline::renderHudRegions(const DisplayJob& job,bool background){
  const auto& plan=job.altitudeDamage;
  const uint32_t started=micros();
  size_t offset=0;
  unsigned previous=0;
  for(unsigned i=roundRegionCursor_.fetch_add(1,std::memory_order_relaxed);i<unsigned(plan.count);
      i=roundRegionCursor_.fetch_add(1,std::memory_order_relaxed)){
    for(;previous<i;++previous)offset+=size_t(plan.regions[previous].width)*plan.regions[previous].height;
    renderHudRegion(job,plan.regions[i],offset,background);
  }
  return micros()-started;
}
void DisplayPipeline::runBackground(){
  bool wasAirspeed=false,wasAltimeter=false,wasHud=false;float previousSpeed=0,previousAltitude=0;uint32_t previousGeneration=0;
  bool hadStableBottom=false,previousDemo=false;
  bool wasRound=false;
  float previousPitch=0,previousRoll=0;
  int previousFpsTenths=-1;
  for(;;){
    RenderRequest request;xQueueReceive(requests_,&request,portMAX_DELAY);
    // CPU1 has submitted all previous foreground bands before starting this
    // request. Rotate the static cache once, then render directly in scan order.
    if(request.job.attitude&&!renderer_->setInstallation(request.job.options.side)){
      Serial.println("ATTITUDE orientation cache allocation failed");
      request.job.attitude=false;
    }
    const bool image=request.job.attitude;
    const bool partial=image&&wasRound&&request.job.generation==previousGeneration;
    const bool stableBottom=image&&Renderer::hasStableBottom(request.pitch,request.roll);
    // Finish the partial frame before selecting its safe scan window.
    request.job.stagedRound=image&&(partial||quarterTurn(request.job.options.side)||!stableBottom);
    const bool keepBottom=stableBottom&&hadStableBottom&&request.job.generation==previousGeneration&&
      request.job.demo==previousDemo&&request.job.fpsTenths==previousFpsTenths;
    const auto viewport=panelRect({0,0,466,keepBottom?Renderer::StableBottomRow:466},request.job.options.side);
    RoundDamage damage;
    if(partial)damage=RoundDamage(previousPitch,previousRoll,request.pitch,request.roll,request.job.options.side,
                                 request.job.options.fps&&request.job.fpsTenths!=previousFpsTenths);
    const int endRow=image?viewport.y+viewport.height:1;
    auto roundRect=[&](int y,int rows){
      const auto full=circleBandBounds(y,rows);
      const int left=std::max(full.x,viewport.x),right=std::min(full.x+full.width,viewport.x+viewport.width);
      const AirspeedRect rect{left,y,right-left,rows};
      return partial?damage.window(rect):rect;
    };
    if(request.job.stagedRound){
      auto& job=request.job;
      xQueueReceive(free_,&job.slot,portMAX_DELAY);
      const uint32_t began=micros();
      job.firstRow=viewport.y;
      for(int y=viewport.y;y<endRow;y+=RotatedBandRows){
        const int rows=std::min(int(RotatedBandRows),endRow-y);
        const auto rect=roundRect(y,rows);
        if(!rect.width)continue;
        auto& planned=job.altitudeDamage;
        if(planned.count){
          auto& prev=planned.regions[planned.count-1];
          if(prev.x==rect.x&&prev.width==rect.width&&prev.y+prev.height==rect.y&&
             size_t(prev.width)*(prev.height+rect.height)<=DisplayHalfBandPixels){prev.height+=rect.height;continue;}
        }
        planned.regions[planned.count++]=rect;
      }
      assert(job.altitudeDamage.count);
      job.firstRow=job.altitudeDamage.regions[0].y;
      renderer_->prepareForeground(request.pitch,request.roll);
      job.renderUs=job.stages[0]=micros()-began;
      roundRegionCursor_.store(0,std::memory_order_relaxed);
      xQueueSend(ready_,&job,portMAX_DELAY);
      job.stages[2]=renderRoundRegions(job,request.pitch,request.roll,true);
      job.renderUs+=job.stages[2];job.backgroundStackFree=uxTaskGetStackHighWaterMark(nullptr);
      xQueueSend(parallelDone_,&job,portMAX_DELAY);
    }else for(int y=viewport.y;y<endRow;y+=DisplayBandRows){
      DisplayJob job=request.job;
      // HUD already owns a prepared slot. Other modes acquire it here.
      // Every job still traverses both FIFO stages; only display releases it.
      if(!job.hud)xQueueReceive(free_,&job.slot,portMAX_DELAY);
      job.firstRow=y;job.rows=std::min(DisplayBandRows,endRow-y);
      job.lastBand=y+job.rows==endRow;
      if(job.altimeter||job.hud){
        const uint32_t began=micros();
        job.fullAirspeed=!(job.hud?wasHud:wasAltimeter)||job.generation!=previousGeneration;
        if(job.fullAirspeed){
          if(job.hud&&job.options.side!=UsbSide::Left){
            // Rotated HUD bands must fit one worker's scratch. Strip order is
            // physical panel order, including after a 90-degree rotation.
            job.altitudeDamage.count=0;
            for(int row=0;row<466;row+=RotatedBandRows)
              job.altitudeDamage.regions[job.altitudeDamage.count++]={0,row,466,std::min(int(RotatedBandRows),466-row)};
          }else{job.altitudeDamage.count=1;job.altitudeDamage.regions[0]={0,0,466,466};}
        }
        else job.altitudeDamage=job.hud?hud_->damagePrepared(job.slot):AltimeterRenderer::damage(previousAltitude,job.altitudeM);
        // Publish the immutable pose before either core draws. Each core owns
        // alternating regions, including background copies, in the same slot.
        if(!job.hud)altimeter_->prepareFrame(job.slot,job.altitudeM);
        job.stages[1]=(job.hud?job.stages[0]:0)+micros()-began;job.renderUs=job.stages[1];
        if(job.hud)roundRegionCursor_.store(0,std::memory_order_relaxed);
        xQueueSend(ready_,&job,portMAX_DELAY);
        size_t offset=0;
        if(job.hud)job.stages[1]+=renderHudRegions(job,true);
        else for(int i=0;i<job.altitudeDamage.count;++i){
          const auto r=job.altitudeDamage.regions[i];
          if(job.hud?HudRenderer::backgroundRegion(i):(i&1)==0){
            uint32_t tick=micros();
            if(job.hud)renderHudRegion(job,r,offset,true);
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
        job.patch=roundRect(y,job.rows);
        renderer_->renderBackgroundWindow(frames_[job.slot],request.pitch,request.roll,job.patch);
        job.renderUs=job.stages[0]=micros()-began;
      }
      job.backgroundStackFree=uxTaskGetStackHighWaterMark(nullptr);
      if(job.altimeter||job.hud)xQueueSend(parallelDone_,&job,portMAX_DELAY);
      else xQueueSend(ready_,&job,portMAX_DELAY);
    }
    wasAirspeed=request.job.airspeed;wasAltimeter=request.job.altimeter;wasHud=request.job.hud;previousSpeed=request.job.speedKmh;previousAltitude=request.job.altitudeM;previousGeneration=request.job.generation;
    hadStableBottom=stableBottom;previousDemo=request.job.demo;previousFpsTenths=request.job.fpsTenths;
    wasRound=image;previousPitch=request.pitch;previousRoll=request.roll;
    // Bound continuous CPU0 work even when all stages remain ready.
    vTaskDelay(1);
  }
}
void DisplayPipeline::presentCanvas(unsigned slot){
  uint16_t* pixels=airFrames_[slot];
  for(int i=0;i<466*466;++i)pixels[i]=uint16_t(pixels[i]<<8|pixels[i]>>8);
  panel_->presentRect(*display_,pixels,0,0,466,466,frames_[slot],466*DisplayBandRows);
}
void DisplayPipeline::presentMenu(const DisplayJob& job){
  JobCanvas canvas(airFrames_[job.slot],job.options.side);
  canvas.fillScreen(RGB565_BLACK);
  canvas.drawCircle(233,233,214,0x31A7);
  constexpr uint16_t accent=0x8EFA,secondary=0xB638;
  const bool direction=job.menu==SettingsPage::Direction;
  static const char* const sideLabels[]={"接口在左","接口在上","接口在右","接口在下"};
  text(canvas,direction?"安装方向":"设置",direction?79:84,direction?1:2,RGB565_WHITE);
  canvas.drawFastHLine(212,direction?117:141,42,accent);
  for(int i=0;i<(direction?4:2);++i){
    const auto r=SettingsMenu::row(job.menu,i);
    const bool selected=direction&&unsigned(job.options.side)==unsigned(i);
    canvas.fillRoundRect(r.x,r.y,r.width,r.height,12,selected?0x192C:0x10A4);
    canvas.drawRoundRect(r.x,r.y,r.width,r.height,12,selected?accent:0x31A7);
    if(direction){
      text(canvas,sideLabels[i],r.y+14,1,selected?accent:RGB565_WHITE);
    }else{
      text(canvas,i==0?"安装方向":"帧率显示",r.y+6,1,RGB565_WHITE);
      const char* value=i==0?sideLabels[unsigned(job.options.side)]:job.options.fps?"开启":"关闭";
      text(canvas,value,r.y+36,1,i==1&&!job.options.fps?secondary:accent);
    }
  }
  text(canvas,direction?"按设置键返回":"按设置键退出",383,1,secondary);
  presentCanvas(job.slot);
}
void DisplayPipeline::presentStatus(const DisplayJob& job,const Snapshot& state){
  JobCanvas canvas(airFrames_[job.slot],job.options.side);
  drawStatus(canvas,state,job.options.fps);presentCanvas(job.slot);
}
void DisplayPipeline::drawStatus(Arduino_GFX& canvas,const Snapshot& state,bool showFps){
  const bool fresh=state.sampledAt&&static_cast<uint32_t>(millis()-state.sampledAt)<=100;
  canvas.fillScreen(RGB565_BLACK);canvas.drawCircle(233,233,205,0x39E7);
  if(!state.present||state.sampleFault||(!fresh&&state.sampledAt)){
    text(canvas,"传感器异常",194,1,RGB565_RED);text(canvas,"正在重试",231,1,0xBDF7);
  }else{
    text(canvas,"请保持静止",177,1,RGB565_WHITE);text(canvas,"正在校准",217,1,RGB565_YELLOW);
    canvas.drawRect(133,260,200,8,0x7BEF);
    canvas.fillRect(134,261,198*state.calibrationCount/StationaryCalibration::RequiredSamples,6,RGB565_YELLOW);
  }
  if(showFps){
    overlay_.update(fps_.value());
    canvas.draw16bitRGBBitmap(FpsOverlay::X,FpsOverlay::Y,overlay_.pixels(),FpsOverlay::Width,FpsOverlay::Height);
  }
}
void DisplayPipeline::presentDial(const DisplayJob& job,DisplayStats& stats){
  const AirspeedRect rect=job.patch;
  uint16_t* pixels=airFrames_[job.slot];
  if(job.options.fps)airOverlay_->update(fps_.value());
  const int count=(job.altimeter||job.hud)?job.altitudeDamage.count:1;
  size_t offset=0;
  for(int i=0;i<count;++i){
    const auto r=(job.altimeter||job.hud)?job.altitudeDamage.regions[i]:rect;
    if(job.hud||job.altimeter||job.fullAirspeed){
      if(job.options.fps)airOverlay_->blitPanelBand(pixels+offset,r,job.hud?job.options.side:UsbSide::Left);
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
  const bool locked=measuredPeriod>=12000&&measuredPeriod<=22000;
  const auto phase=job.hud&&locked&&!job.fullAirspeed?roundPhase(job.altitudeDamage,measuredPeriod):
    RoundPhase{job.hud?(locked?measuredPeriod/4:4000):0,0};
  sync_.wait(phase.first,job.hud&&locked&&!job.fullAirspeed,phase.last);
  const uint32_t edge=sync_.selectedEdge(),period=sync_.period();
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
    panel_->presentRect(*display_,pixels+offset,r.x,r.y,r.width,r.height,frames_[job.slot],466*DisplayBandRows,job.options.side);
    stats.setupUs+=panel_->setupMicros();offset+=r.width*r.height;
  }
  stats.patchPixels=offset;
  const int tenths=job.options.fps?airOverlay_->tenths():-2;
  if(job.options.fps&&!job.hud&&!job.fullAirspeed&&tenths!=airFpsTenths_){
    // The badge begins at odd X=181. Restore its extra border pixel and send
    // the enclosing even-aligned window, keeping the badge visually centered.
    const AirspeedRect badge={FpsOverlay::X-1,FpsOverlay::Y,FpsOverlay::Width+2,FpsOverlay::Height};
    if(job.altimeter)altimeter_->renderBackgroundRectangle(pixels,badge);
    else airspeed_->renderBackgroundRectangle(pixels,badge);
    airOverlay_->blitBand(pixels,badge.width,badge.y,badge.height,badge.x);
    panel_->presentRect(*display_,pixels,badge.x,badge.y,badge.width,badge.height,frames_[job.slot],466*DisplayBandRows,job.options.side);
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
  uint32_t roundEdge=0;
  uint32_t roundScanCrosses=0,hudScanCrosses=0;
  bool discard=false;
  for(;;){
    DisplayJob job;xQueueReceive(jobs_,&job,portMAX_DELAY);
    if(discard&&job.sequence!=discardSequence)discard=false;
    if(job.generation!=generation()||(discard&&job.sequence==discardSequence)){
      xQueueSend(free_,&job.slot,portMAX_DELAY);vTaskDelay(1);continue;
    }
    if(job.menu!=SettingsPage::Closed){
      presentMenu(job);fps_={};lastCompleted=0;
      xQueueSend(free_,&job.slot,portMAX_DELAY);continue;
    }
    if(job.hud&&!job.altitudeDamage.count){xQueueSend(free_,&job.slot,portMAX_DELAY);continue;}
    const bool firstBand=job.sequence!=activeSequence;
    if(firstBand){activeSequence=job.sequence;frameStats={};}
    Snapshot live;xQueuePeek(snapshots_,&live,0);
    const bool current=live.calibrated&&live.sampledAt&&static_cast<uint32_t>(millis()-live.sampledAt)<=100;
    const bool jobFresh=job.state.sampledAt&&static_cast<uint32_t>(millis()-job.state.sampledAt)<=100;
    const FrameDecision decision=job.hud?decideHudFrame(job.demo,current,jobFresh,job.hudInput.attitudeValid):
      decideFrame(job.airspeed||job.altimeter,job.attitude,job.demo,current,jobFresh);
    if(decision==FrameDecision::Discard){
      // A discarded first HUD frame never cleared the previous instrument.
      // Force its successor to rebuild the entire screen, not only HUD bands.
      if(job.hud||job.attitude)invalidate();
      discard=true;discardSequence=job.sequence;
      xQueueSend(free_,&job.slot,portMAX_DELAY);vTaskDelay(1);continue;
    }
    if(firstBand&&decision==FrameDecision::Draw&&job.attitude){
      if(job.stagedRound&&(job.options.fps||job.demo)){
        size_t offset=0;
        if(job.options.fps)airOverlay_->update(job.fpsTenths<0?-1.f:job.fpsTenths*.1f);
        for(int i=0;i<job.altitudeDamage.count;++i){
          const auto rect=job.altitudeDamage.regions[i];
          auto* pixels=airFrames_[job.slot]+offset;
          if(job.options.fps)airOverlay_->blitPanelBand(pixels,rect,job.options.side);
          if(job.demo)airOverlay_->blitPanelLegend(pixels,rect,job.options.side,true);
          offset+=rect.width*rect.height;
        }
      }
      const uint32_t period=sync_.period();
      const bool locked=period>=12000&&period<=22000;
      // Relative to the first scan row we update, not just to vertical blank.
      const uint32_t tickPeriod=locked?period:16480;
      const RoundPhase phase=job.stagedRound?roundPhase(job.altitudeDamage,tickPeriod):
        RoundPhase{tickPeriod*job.firstRow/466+1800,0};
      const bool synced=sync_.wait(phase.first,locked,phase.last);
      roundEdge=synced?sync_.selectedEdge():0;
      frameStats.tePeriodUs=sync_.period();frameStats.tePulses=sync_.pulses();frameStats.teMisses=sync_.misses();
      if(roundEdge){frameStats.scanMinUs=1000000;frameStats.scanMaxUs=-1000000;}
    }
    const uint32_t began=micros();
    bool completedFrame=false,statusFrame=false;
    if(decision==FrameDecision::Draw&&(job.airspeed||job.altimeter||job.hud)){
      presentDial(job,frameStats);
      completedFrame=true;
    }else if(decision==FrameDecision::Draw&&job.stagedRound){
      const auto& damage=job.altitudeDamage;
      panel_->presentRegions(*display_,airFrames_[job.slot],damage.regions,damage.count,
                             frames_[job.slot],466*DisplayBandRows);
      frameStats.setupUs=panel_->setupMicros();frameStats.renderUs=job.renderUs;
      for(unsigned i=0;i<4;++i)frameStats.stages[i]=job.stages[i];
      frameStats.backgroundStackFree=job.backgroundStackFree;
      for(int i=0;i<damage.count;++i){
        const auto r=damage.regions[i];const auto& timing=panel_->regionTiming(i);
        frameStats.patchPixels+=r.width*r.height;
        if(!roundEdge||!timing.began||!timing.ended)continue;
        const int32_t first=int32_t(frameStats.tePeriodUs*r.y/466)-int32_t(timing.began-roundEdge);
        const int32_t last=int32_t(frameStats.tePeriodUs*(r.y+r.height)/466)-int32_t(timing.ended-roundEdge);
        frameStats.scanMinUs=std::min(frameStats.scanMinUs,std::min(first,last));
        frameStats.scanMaxUs=std::max(frameStats.scanMaxUs,std::max(first,last));
      }
      completedFrame=true;
    }else if(decision==FrameDecision::Draw){
      const auto r=job.patch;
      panel_->presentRect(*display_,frames_[job.slot],r.x,r.y,r.width,r.height,nullptr,0);
      if(roundEdge){
        const int32_t first=int32_t(frameStats.tePeriodUs*job.firstRow/466)-int32_t(began-roundEdge);
        const int32_t last=int32_t(frameStats.tePeriodUs*(job.firstRow+job.rows)/466)-int32_t(micros()-roundEdge);
        frameStats.scanMinUs=std::min(frameStats.scanMinUs,std::min(first,last));
        frameStats.scanMaxUs=std::max(frameStats.scanMaxUs,std::max(first,last));
      }
      frameStats.patchPixels+=r.width*r.height;
      frameStats.renderUs+=job.renderUs;
      for(unsigned i=0;i<4;++i)frameStats.stages[i]+=job.stages[i];
      frameStats.setupUs+=panel_->setupMicros();
      frameStats.backgroundStackFree=job.backgroundStackFree;
      if(job.lastBand){
        completedFrame=true;
      }
    }else{
      presentStatus(job,live);frameStats={};completedFrame=true;statusFrame=true;
      if(job.attitude)invalidate(); // Restoring an instrument now requires its entire face.
      // A sensor fault mid-frame clears the screen once, then drops all remaining bands.
      discard=true;discardSequence=job.sequence;
    }
    const uint32_t completed=micros();
    if(!job.airspeed&&!job.altimeter&&!job.hud)frameStats.transferUs+=completed-began;
    if(completedFrame){
      if(job.attitude&&!statusFrame&&roundEdge&&
         (frameStats.scanMaxUs>=0||frameStats.scanMinUs<=-int32_t(frameStats.tePeriodUs)))++roundScanCrosses;
      if(job.hud&&!job.fullAirspeed&&!statusFrame&&
         (frameStats.scanMaxUs>=0||frameStats.scanMinUs<=-int32_t(frameStats.tePeriodUs)))++hudScanCrosses;
      const uint32_t maxGap=stats.maxGapUs;
      stats=frameStats;stats.maxGapUs=maxGap;
      stats.roundScanCrosses=roundScanCrosses;
      stats.hudScanCrosses=hudScanCrosses;
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
