#include "renderer.h"
#include "instrument_style.h"
// Favor rendering throughput over code size, without fast-math assumptions.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

namespace instrument {
struct FixedPixel { uint32_t index; uint16_t color; float alpha; };
uint32_t renderStages[4]={};
namespace {
const int N=466, PIXELS=N*N;
const float C=232.5f, R=232.5f, BALL=174.0f, PI=3.14159265358979323846f;
float clamp(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
uint16_t blend(uint16_t a,uint16_t b,float t){
  const float u=1-t;
  return (int(((a>>11)&31)*u+((b>>11)&31)*t+.5f)<<11) |
         (int(((a>>5)&63)*u+((b>>5)&63)*t+.5f)<<5) |
          int((a&31)*u+(b&31)*t+.5f);
}
void* allocate(size_t size){
#if defined(ARDUINO_ARCH_ESP32)
  return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return std::malloc(size);
#endif
}
struct Canvas {
  uint16_t* f; float limit;
  FixedPixel* recorded; size_t* count;
  Canvas(uint16_t* frame,float radius,FixedPixel* operations=nullptr,size_t* size=nullptr)
    :f(frame),limit(radius),recorded(operations),count(size){}
  void pixel(int x,int y,uint16_t color,float alpha){
    if(x<0||x>=N||y<0||y>=N)return;
    float dx=x-C,dy=y-C;
    if(dx*dx+dy*dy>limit*limit)return;
    if(alpha<=0)return;
    if(count){
      if(recorded)recorded[*count]={static_cast<uint32_t>(y*N+x),color,alpha};
      ++*count;return;
    }
    f[y*N+x]=alpha>=1?color:blend(f[y*N+x],color,alpha);
  }
  void pitchDigit(int digit,float x,float y,float cr,float sr){
    style::paintPitchDigit(*this,digit,x,y,cr,sr,0,N,0,N);
  }
  void line(float x1,float y1,float x2,float y2,float width,uint16_t col){
    float vx=x2-x1,vy=y2-y1,len=vx*vx+vy*vy,half=width*.5f;
    const float inverseLength=len>0?1.f/len:0;
    int minx=int(std::floor(std::fmin(x1,x2)-half-1)),maxx=int(std::ceil(std::fmax(x1,x2)+half+1));
    int miny=int(std::floor(std::fmin(y1,y2)-half-1)),maxy=int(std::ceil(std::fmax(y1,y2)+half+1));
    if(minx<0)minx=0;if(maxx>=N)maxx=N-1;if(miny<0)miny=0;if(maxy>=N)maxy=N-1;
    for(int y=miny;y<=maxy;++y)for(int x=minx;x<=maxx;++x){
      float t=len>0?clamp(((x-x1)*vx+(y-y1)*vy)*inverseLength,0,1):0;
      float dx=x-x1-t*vx,dy=y-y1-t*vy;
      float d2=dx*dx+dy*dy;
      if(half>=.5f && d2<=(half-.5f)*(half-.5f))pixel(x,y,col,1);
      else if(d2<(half+.5f)*(half+.5f))pixel(x,y,col,half+.5f-std::sqrt(d2));
    }
  }
  void triangle(float ax,float ay,float bx,float by,float cx,float cy,uint16_t color){
    int minx=int(std::floor(std::fmin(ax,std::fmin(bx,cx)))),maxx=int(std::ceil(std::fmax(ax,std::fmax(bx,cx))));
    int miny=int(std::floor(std::fmin(ay,std::fmin(by,cy)))),maxy=int(std::ceil(std::fmax(ay,std::fmax(by,cy))));
    float sign=((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))>=0?1.f:-1.f;
    for(int y=miny;y<=maxy;++y)for(int x=minx;x<=maxx;++x){
      if(sign*((bx-ax)*(y-ay)-(by-ay)*(x-ax))>=0 && sign*((cx-bx)*(y-by)-(cy-by)*(x-bx))>=0 && sign*((ax-cx)*(y-cy)-(ay-cy)*(x-cx))>=0)pixel(x,y,color,1);
    }
    line(ax,ay,bx,by,1,color);line(bx,by,cx,cy,1,color);line(cx,cy,ax,ay,1,color);
  }
};
}
struct Renderer::Data {
  uint16_t sky[PIXELS],ground[PIXELS]; int16_t depth[PIXELS];
  uint8_t horizonVisibility[PIXELS];
  FixedPixel* fixedPixels; size_t fixedCount;
};
Renderer::Renderer():data_(nullptr){}
Renderer::~Renderer(){if(data_)std::free(data_->fixedPixels);std::free(data_);}
bool Renderer::begin(){
  if(data_)return true;
  data_=static_cast<Data*>(allocate(sizeof(Data)));
  if(!data_)return false;
  // Record static geometry once. Replay preserves the original antialiasing and
  // layer order without repeating distance tests, division or square roots.
  data_->fixedCount=0;data_->fixedPixels=nullptr;
  Canvas counter(nullptr,R,nullptr,&data_->fixedCount);style::fixed(counter);
  data_->fixedPixels=static_cast<FixedPixel*>(allocate(data_->fixedCount*sizeof(FixedPixel)));
  if(!data_->fixedPixels){std::free(data_);data_=nullptr;return false;}
  size_t recorded=0;
  Canvas recorder(nullptr,R,data_->fixedPixels,&recorded);style::fixed(recorder);
  for(int y=0;y<N;++y)for(int x=0;x<N;++x){
    int i=y*N+x;float dx=x-C,dy=y-C,r2=dx*dx+dy*dy;
    if(r2>R*R){data_->sky[i]=data_->ground[i]=0;data_->depth[i]=-1;continue;}
    float radius=r2<BALL*BALL?BALL:R;
    float depth=std::sqrt(std::fmax(0.f,radius*radius-r2));
    // Bit 14 marks the annulus; sphere depth occupies the lower 14 bits.
    data_->depth[i]=int16_t(depth*64) | (radius==R?0x4000:0);
    data_->horizonVisibility[i]=style::background(x,y,data_->sky[i],data_->ground[i]);
  }
  return true;
}
void Renderer::render(uint16_t* frame,float pitch,float roll){
  if(!frame)return;
  if(!data_||!std::isfinite(pitch)||!std::isfinite(roll)){std::memset(frame,0,PIXELS*sizeof(uint16_t));return;}
  pitch=clamp(pitch,-90,90)*PI/180;roll=std::fmod(roll,360.f)*PI/180;
  float cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
#if defined(ARDUINO_ARCH_ESP32)
  int64_t stamp=esp_timer_get_time();
#endif
  // Q18 screen coordinates avoid floating-point work for every background pixel.
  // Products stay below 2^28; depth uses Q6, hence the Q12 depth coefficient.
  const int32_t scale=1<<18;
  const int32_t outerStep=std::lround(sr*scale),innerStep=std::lround(sr*cp*scale);
  const int32_t depthStep=std::lround(sp*4096.f);
  for(int y=0;y<N;++y){
    float rotated=(y-C)*cr-C*sr;
    int32_t outer=std::lround(rotated*scale),inner=std::lround(rotated*cp*scale);
    for(int x=0;x<N;++x,outer+=outerStep,inner+=innerStep){
      int i=y*N+x;int depth=data_->depth[i];
      if(depth<0){frame[i]=0;continue;}
      int32_t elevation=(depth & 0x4000)?outer:inner-depth*depthStep;
      frame[i]=elevation<0?data_->sky[i]:data_->ground[i];
      int32_t distance=std::abs(elevation);
      if(distance<style::HorizonEdgeQ18 && data_->sky[i]!=data_->ground[i])
        frame[i]=blend(frame[i],style::White,clamp(style::HorizonEdge-distance*(1.f/262144.f),0,1)*(data_->horizonVisibility[i]*(1.f/255.f)));
    }
  }
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[0]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();
#endif
  Canvas c={frame,BALL-7};
  style::ladder(c,pitch,cr,sr);
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[1]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();
#endif
  style::bank(c,roll);
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[2]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();
#endif
  for(size_t i=0;i<data_->fixedCount;++i){
    const FixedPixel& p=data_->fixedPixels[i];
    frame[p.index]=p.alpha>=1?p.color:blend(frame[p.index],p.color,p.alpha);
  }
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[3]=esp_timer_get_time()-stamp;
#endif
}
}
