#pragma once
#include <cstdint>
namespace instrument {
class FpsCounter {
 public:
  void frameCompleted(uint32_t now){
    if(!started_){started_=true;since_=now;return;}
    ++frames_;
    const uint32_t elapsed=now-since_;
    if(elapsed>=1000000u){value_=frames_*1000000.f/elapsed;frames_=0;since_=now;}
  }
  float value() const{return value_;}
 private:
  uint32_t since_=0,frames_=0;
  float value_=-1;
  bool started_=false;
};
class FpsOverlay {
 public:
  // Centered at the bottom, below the inner ring and the DEMO legend.
  static constexpr int X=181,Y=432,Width=104,Height=22;
  FpsOverlay():FpsOverlay(false){}
  explicit FpsOverlay(bool wireOrder):wireOrder_(wireOrder){update(-1);}
  void update(float fps);
  void blit(uint16_t* frame,int stride) const;
  // Memory origin corresponds to (firstColumn, firstRow) in the full frame.
  void blitBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn=0) const;
  // Transparent yellow DEMO legend at (209,414), within a 48x14 rectangle.
  void blitDemoBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn=0) const;
  // Opaque SIM backing keeps a full-circle dial's ticks and needles out of the label.
  void blitSimBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn=0,bool opaque=false) const;
  uint16_t* pixels(){return pixels_;}
 private:
  void blitLegend(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn,const char* text,int count,bool opaque=false) const;
  uint16_t pixels_[Width*Height];
  int lastTenths_=-2;
  bool wireOrder_;
};
}
