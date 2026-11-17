#include "renderer.h"
// Favor rendering throughput over code size, without fast-math assumptions.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
#include "instrument_style.h"
#include "circle_band.h"
#include "fps_overlay.h"
#include "raster_math.h"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#undef PI
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_memory_utils.h>
#define ROUND_HOT IRAM_ATTR
#else
#define ROUND_HOT
#endif

namespace instrument {
struct FixedPixel { uint32_t index; uint16_t color; float alpha; };
struct DrawCommand {
  float x1,y1,x2,y2,x3,y3,width,limit;
  enum Kind : uint8_t {Line,Triangle,PitchDigit};
  uint16_t color;Kind kind;uint8_t digit;
};
uint32_t renderStages[4]={};
#if defined(INSTRUMENT_TEST_CACHE)
int backgroundAllocationFailure=-1;
size_t backgroundCacheStats[5]={};
#endif
#if defined(INSTRUMENT_TEST_POSE_CACHE)
size_t backgroundPoseBuilds=0,backgroundPoseCommands=0,backgroundPoseOverflows=0,backgroundPoseBankOverflows=0;
#endif
namespace {
#if !defined(INSTRUMENT_COMMAND_CAPACITY)
#define INSTRUMENT_COMMAND_CAPACITY 320
#endif
const size_t CommandCapacity=INSTRUMENT_COMMAND_CAPACITY;
const int N=466, PIXELS=N*N;
const float C=232.5f, R=232.5f, BALL=174.0f, PI=3.14159265358979323846f;
constexpr int CommandBandRows=28;
constexpr int CommandBands=(N+CommandBandRows-1)/CommandBandRows;
constexpr size_t CommandWords=(CommandCapacity+31)/32;
uint16_t swapBytes(uint16_t color){return static_cast<uint16_t>((color<<8)|(color>>8));}
float clamp(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
uint16_t ROUND_HOT blend(uint16_t a,uint16_t b,float t){
  const float u=1-t;
  return (int(((a>>11)&31)*u+((b>>11)&31)*t+.5f)<<11) |
         (int(((a>>5)&63)*u+((b>>5)&63)*t+.5f)<<5) |
          int((a&31)*u+(b&31)*t+.5f);
}
void* allocate(size_t size,bool preferInternal=false){
#if defined(INSTRUMENT_TEST_CACHE)
  if(backgroundAllocationFailure==0)return nullptr;
  if(backgroundAllocationFailure>0)--backgroundAllocationFailure;
#endif
#if defined(ARDUINO_ARCH_ESP32)
  // Leave room for the queues and tasks created after renderer initialization.
  if(preferInternal && heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>=size+64*1024){
    void* memory=heap_caps_malloc(size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(memory)return memory;
  }
  return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  (void)preferInternal;
  return std::malloc(size);
#endif
}
struct Canvas {
  uint16_t* f; float limit;
  FixedPixel* recorded; size_t* count;
  int firstRow,endRow,firstColumn,stride;
  bool wireOrder;
  DrawCommand* commands=nullptr;size_t* commandCount=nullptr;bool* commandOverflow=nullptr;
  Canvas(uint16_t* frame,float radius,FixedPixel* operations=nullptr,size_t* size=nullptr,int first=0,int end=N,bool wire=false,int column=0,int width=N)
    :f(frame),limit(radius),recorded(operations),count(size),firstRow(first),endRow(end),firstColumn(column),stride(width),wireOrder(wire){}
  void ROUND_HOT pixel(int x,int y,uint16_t color,float alpha){
    if(x<firstColumn||x>=firstColumn+stride||y<firstRow||y>=endRow)return;
    float dx=x-C,dy=y-C;
    if(dx*dx+dy*dy>limit*limit)return;
    if(alpha<=0)return;
    if(count){
      if(recorded)recorded[*count]={static_cast<uint32_t>(y*N+x),color,alpha};
      ++*count;return;
    }
    const int index=(y-firstRow)*stride+x-firstColumn;
    if(wireOrder)f[index]=swapBytes(alpha>=1?color:blend(swapBytes(f[index]),color,alpha));
    else f[index]=alpha>=1?color:blend(f[index],color,alpha);
  }
  void ROUND_HOT line(float x1,float y1,float x2,float y2,float width,uint16_t col){
    if(commandCount){
      if(*commandCount<CommandCapacity)commands[(*commandCount)++]={x1,y1,x2,y2,0,0,width,limit,col,DrawCommand::Line,0};
      else *commandOverflow=true;
      return;
    }
    const float half=width*.5f,padding=half+1;
    // Reject invisible bands before bounding-box math and distance setup.
    // The spare half pixel includes every round cap and antialiased edge.
    if((y1+padding<firstRow && y2+padding<firstRow) ||
       (y1-padding>=endRow && y2-padding>=endRow))return;
    float vx=x2-x1,vy=y2-y1,len=vx*vx+vy*vy;
    // A stroke contributes only within half-width + 0.5 pixels. The extra
    // 0.001 protects coordinate rounding while excluding guaranteed-zero pixels.
    int minx=ceilBound(minBound(x1,x2)-half-.501f),maxx=floorBound(maxBound(x1,x2)+half+.501f);
    int miny=ceilBound(minBound(y1,y2)-half-.501f),maxy=floorBound(maxBound(y1,y2)+half+.501f);
    if(minx<firstColumn)minx=firstColumn;if(maxx>=firstColumn+stride)maxx=firstColumn+stride-1;
    if(miny<firstRow)miny=firstRow;if(maxy>=endRow)maxy=endRow-1;
    if(minx>maxx || miny>maxy)return;
    const float farX=maxBound(C-minx,maxx-C),farY=maxBound(C-miny,maxy-C);
    const bool direct=!count && farX*farX+farY*farY<=limit*limit;
    const uint16_t opaque=wireOrder?swapBytes(col):col;
    const float inverseLength=len>0?1.f/len:0;
    const float inverseDistance=len>0?1.f/std::sqrt(len):0;
    const bool clipStrip=!count && std::fabs(vy)>.1f*std::fabs(vx);
    const float slope=clipStrip?vx/vy:0;
    const float stripHalf=clipStrip?(half+.501f)/(std::fabs(vy)*inverseDistance):0;
    for(int y=miny;y<=maxy;++y){
      int first=minx,last=maxx;
      if(clipStrip){
        // The entire round-capped segment lies inside this infinite strip.
        // Keep a rounding margin and intersect it with the finite bounds.
        const float center=x1+(y-y1)*slope;
        first=ceilBound(maxBound(float(minx),center-stripHalf-.002f));
        last=floorBound(minBound(float(maxx),center+stripHalf+.002f));
      }
      uint16_t* out=direct?f+(y-firstRow)*stride:nullptr;
      const float dy0=y-y1;
      for(int x=first;x<=last;++x){
        const float dx0=x-x1,projection=dx0*vx+dy0*vy;
        float alpha=0;
        if(!count && len>0 && projection>=0 && projection<=len){
          const float distance=std::fabs(dx0*vy-dy0*vx)*inverseDistance;
          if(distance<=half-.5f)alpha=1;
          else if(distance<half+.5f)alpha=half+.5f-distance;
        }else{
          // Preserve the original cap distance and fixed-geometry coverage.
          const float t=len>0?clamp(projection*inverseLength,0,1):0;
          const float dx=dx0-t*vx,dy=dy0-t*vy,d2=dx*dx+dy*dy;
          if(half>=.5f && d2<=(half-.5f)*(half-.5f))alpha=1;
          else if(d2<(half+.5f)*(half+.5f))alpha=half+.5f-std::sqrt(d2);
        }
        if(alpha<=0)continue;
        if(!direct){pixel(x,y,col,alpha);continue;}
        uint16_t& target=out[x-firstColumn];
        if(alpha>=1)target=opaque;
        else target=wireOrder?swapBytes(blend(swapBytes(target),col,alpha)):blend(target,col,alpha);
      }
    }
  }
  void ROUND_HOT triangle(float ax,float ay,float bx,float by,float cx,float cy,uint16_t color){
    if(commandCount){
      if(*commandCount<CommandCapacity)commands[(*commandCount)++]={ax,ay,bx,by,cx,cy,1,limit,color,DrawCommand::Triangle,0};
      else *commandOverflow=true;
      return;
    }
    // Include the width-one edge strokes as well as the solid triangle.
    if((ay+1.5f<firstRow && by+1.5f<firstRow && cy+1.5f<firstRow) ||
       (ay-1.5f>=endRow && by-1.5f>=endRow && cy-1.5f>=endRow))return;
    int minx=floorBound(minBound(ax,minBound(bx,cx))),maxx=ceilBound(maxBound(ax,maxBound(bx,cx)));
    int miny=floorBound(minBound(ay,minBound(by,cy))),maxy=ceilBound(maxBound(ay,maxBound(by,cy)));
    if(minx<firstColumn)minx=firstColumn;if(maxx>=firstColumn+stride)maxx=firstColumn+stride-1;
    if(miny<firstRow)miny=firstRow;if(maxy>=endRow)maxy=endRow-1;
    float sign=((bx-ax)*(cy-ay)-(by-ay)*(cx-ax))>=0?1.f:-1.f;
    for(int y=miny;y<=maxy;++y)for(int x=minx;x<=maxx;++x){
      if(sign*((bx-ax)*(y-ay)-(by-ay)*(x-ax))>=0 && sign*((cx-bx)*(y-by)-(cy-by)*(x-bx))>=0 && sign*((ax-cx)*(y-cy)-(ay-cy)*(x-cx))>=0)pixel(x,y,color,1);
    }
    line(ax,ay,bx,by,1,color);line(bx,by,cx,cy,1,color);line(cx,cy,ax,ay,1,color);
  }
  void ROUND_HOT pitchDigit(int digit,float x,float y,float cr,float sr){
    if(commandCount){
      if(*commandCount<CommandCapacity)
        commands[(*commandCount)++]={x,y,cr,sr,0,0,0,limit,style::White,DrawCommand::PitchDigit,uint8_t(digit)};
      else *commandOverflow=true;
      return;
    }
    style::paintPitchDigit(*this,digit,x,y,cr,sr,firstRow,endRow,firstColumn,firstColumn+stride);
  }
  void ROUND_HOT replay(const DrawCommand* commands,const uint32_t* visible,size_t first,size_t end){
    for(size_t i=first;i<end;){
      const size_t word=i/32;
      const uint32_t selected=visible[word]&(~uint32_t(0)<<(i%32));
      if(!selected){i=(word+1)*32;continue;}
      i=word*32+__builtin_ctz(selected);
      if(i>=end)break;
      const DrawCommand& p=commands[i];limit=p.limit;
      if(p.kind==DrawCommand::Triangle)triangle(p.x1,p.y1,p.x2,p.y2,p.x3,p.y3,p.color);
      else if(p.kind==DrawCommand::PitchDigit)pitchDigit(p.digit,p.x1,p.y1,p.x2,p.y2);
      else line(p.x1,p.y1,p.x2,p.y2,p.width,p.color);
      ++i;
    }
  }
};
// Copy complete sky/ground spans as aligned words on ESP32. Both pointers may
// be only RGB565-aligned, so mismatched word alignment uses the portable copy.
void ROUND_HOT copySpan(uint16_t* dst,const uint16_t* src,int count){
#if defined(__GNUC__) && (defined(ARDUINO_ARCH_ESP32) || defined(INSTRUMENT_TEST_WORD_COPY))
  typedef uint32_t Word __attribute__((__may_alias__));
  if((reinterpret_cast<uintptr_t>(dst)^reinterpret_cast<uintptr_t>(src))&3){
    std::memcpy(dst,src,count*sizeof(uint16_t));return;
  }
  if(count && (reinterpret_cast<uintptr_t>(dst)&3)){*dst++=*src++;--count;}
  Word* out=reinterpret_cast<Word*>(dst);
  const Word* in=reinterpret_cast<const Word*>(src);
  int words=count/2;
  for(;words>=8;words-=8,out+=8,in+=8){
    const Word a=in[0],b=in[1],c=in[2],d=in[3],e=in[4],f=in[5],g=in[6],h=in[7];
    out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;out[7]=h;
  }
  for(;words;--words)*out++=*in++;
  if(count&1)*reinterpret_cast<uint16_t*>(out)=*reinterpret_cast<const uint16_t*>(in);
#else
  std::memcpy(dst,src,count*sizeof(uint16_t));
#endif
}
struct BackgroundRow {
  uint16_t* frame;const uint16_t* sky;const uint16_t* ground;const int16_t* depth;const uint8_t* horizonVisibility;
  int32_t base,step,depthStep;
  int firstColumn;
  bool inner,wireOrder;
  int32_t elevation(int x)const{return base+x*step-(inner?depth[x]*depthStep:0);}
  void ROUND_HOT shade(int first,int end)const{
    int32_t plane=base+first*step;
    for(int x=first;x<end;++x,plane+=step){
      const int32_t e=plane-(inner?depth[x]*depthStep:0);
      const uint16_t s=sky[x],g=ground[x];
      uint16_t color=e<0?s:g;
      const int32_t distance=std::abs(e);
      if(distance<style::HorizonEdgeQ18 && s!=g){
        const float alpha=clamp(style::HorizonEdge-distance*(1.f/262144.f),0,1)*(horizonVisibility[x]*(1.f/255.f));
        color=wireOrder?swapBytes(blend(swapBytes(color),style::White,alpha)):blend(color,style::White,alpha);
      }
      frame[x-firstColumn]=color;
    }
  }
  void ROUND_HOT spans(int first,int end,const float* roots,int rootCount)const{
    // Up to four algebraic crossings. A three-pixel window absorbs floating
    // point root rounding; the guard threshold also covers Q6/Q18 error.
    int cuts[10],low[4],high[4],count=0;
    cuts[count++]=first;cuts[count++]=end;
    int windows=0;
    for(int i=0;i<rootCount;++i){
      const float screen=roots[i]+C;
      if(screen<first-4 || screen>end+4)continue;
      int lo=floorBound(screen)-3,hi=ceilBound(screen)+4;
      if(lo>=end || hi<=first)continue;
      if(lo<first)lo=first;if(hi>end)hi=end;
      low[windows]=lo;high[windows++]=hi;
      cuts[count++]=lo;cuts[count++]=hi;
    }
    for(int i=1;i<count;++i){int v=cuts[i],j=i;while(j && cuts[j-1]>v){cuts[j]=cuts[j-1];--j;}cuts[j]=v;}
    for(int i=1;i<count;++i){
      const int lo=cuts[i-1],hi=cuts[i];if(lo==hi)continue;
      bool edge=false;
      for(int j=0;j<windows;++j)if(lo<high[j] && hi>low[j]){edge=true;break;}
      const int32_t e=elevation((lo+hi-1)/2);
      if(edge || std::abs(e)<style::HorizonGuardQ18)shade(lo,hi);
      else copySpan(frame+(lo-firstColumn),(e<0?sky:ground)+lo,hi-lo);
    }
  }
};
}
struct Renderer::Data {
  UsbSide side;
  uint16_t sky[PIXELS],ground[PIXELS]; int16_t depth[PIXELS];
  uint8_t horizonVisibility[PIXELS];
  struct RowSpan {int16_t first,end,ballFirst,ballEnd;} rows[N];
  FixedPixel* fixedPixels; size_t fixedCount;
  uint32_t fixedRowStart[N+1];
  struct FixedSpan {uint32_t index,offset;uint16_t count;};
  FixedSpan* fixedSpans;uint16_t* fixedColors;size_t fixedSpanCount;
  uint16_t spanRowStart[N+1];
  DrawCommand* commands;
  size_t commandCount,bankStart;
  uint32_t commandBands[CommandBands][CommandWords];
  float posePitch,poseRoll;bool poseValid,poseOverflow;
  bool cacheFixed(){
    // The gradients have not been initialized yet: borrow their storage for
    // final fixed colors and a map of pixels touched by an opaque operation.
    std::memset(sky,0,sizeof(sky));std::memset(ground,0,sizeof(ground));
    for(size_t i=0;i<fixedCount;++i){
      const FixedPixel& p=fixedPixels[i];
      if(p.alpha>=1){sky[p.index]=p.color;ground[p.index]=1;}
      else sky[p.index]=blend(sky[p.index],p.color,p.alpha);
    }
    size_t edgeCount=0,spanCount=0,colorCount=0,opaqueCount=0;
    for(size_t i=0;i<fixedCount;++i)if(!ground[fixedPixels[i].index])++edgeCount;
    for(int i=0;i<PIXELS;){
      if(!ground[i]){++i;continue;}
      const int first=i,rowEnd=(first/N+1)*N;
      while(i<rowEnd && ground[i])++i;
      // Match the frame pixel's word alignment for the ESP32 span copier.
      if((colorCount^size_t(first))&1)++colorCount;
      colorCount+=i-first;opaqueCount+=i-first;++spanCount;
    }
    FixedPixel* edges=edgeCount?static_cast<FixedPixel*>(allocate(edgeCount*sizeof(FixedPixel),true)):nullptr;
    FixedSpan* spans=spanCount?static_cast<FixedSpan*>(allocate(spanCount*sizeof(FixedSpan),true)):nullptr;
    uint16_t* colors=colorCount?static_cast<uint16_t*>(allocate(colorCount*sizeof(uint16_t),true)):nullptr;
    if((edgeCount&&!edges)||(spanCount&&!spans)||(colorCount&&!colors)){
      std::free(edges);std::free(spans);std::free(colors);return false;
    }
    // Stable counting sort by row: every band visits only its own shadows and
    // edges. Reverse placement preserves alpha layer order within each pixel,
    // using the prefix array itself as cursors, with no temporary allocation.
    std::memset(fixedRowStart,0,sizeof(fixedRowStart));
    for(size_t i=0;i<fixedCount;++i)
      if(!ground[fixedPixels[i].index])++fixedRowStart[fixedPixels[i].index/N+1];
    for(int y=1;y<=N;++y)fixedRowStart[y]+=fixedRowStart[y-1];
    for(size_t i=fixedCount;i>0;--i){
      const FixedPixel& p=fixedPixels[i-1];
      if(!ground[p.index])edges[--fixedRowStart[p.index/N+1]]=p;
    }
    for(int y=0;y<N;++y)fixedRowStart[y]=fixedRowStart[y+1];
    fixedRowStart[N]=edgeCount;
    size_t span=0,offset=0;
    for(int i=0;i<PIXELS;){
      if(!ground[i]){++i;continue;}
      const int first=i,rowEnd=(first/N+1)*N;
      while(i<rowEnd && ground[i])++i;
      if((offset^size_t(first))&1)++offset;
      spans[span++]={static_cast<uint32_t>(first),static_cast<uint32_t>(offset),static_cast<uint16_t>(i-first)};
      std::memcpy(colors+offset,sky+first,(i-first)*sizeof(uint16_t));offset+=i-first;
    }
#if defined(INSTRUMENT_TEST_CACHE)
    backgroundCacheStats[0]=fixedCount;backgroundCacheStats[1]=edgeCount;
    backgroundCacheStats[2]=spanCount;
    backgroundCacheStats[3]=edgeCount*sizeof(FixedPixel)+spanCount*sizeof(FixedSpan)+colorCount*sizeof(uint16_t);
    backgroundCacheStats[4]=opaqueCount;
#endif
    (void)opaqueCount;
    std::free(fixedPixels);fixedPixels=edges;fixedCount=edgeCount;
    fixedSpans=spans;fixedColors=colors;fixedSpanCount=spanCount;return true;
  }
};
RoundDamage::RoundDamage(float previousPitch,float previousRoll,float pitch,float roll,UsbSide side,bool fpsChanged){
  if(!std::isfinite(previousPitch)||!std::isfinite(previousRoll)||!std::isfinite(pitch)||!std::isfinite(roll))return;
  valid_=true;
  const float pitches[]={previousPitch,pitch},rolls[]={previousRoll,roll};
  for(int i=0;i<2;++i){
    const float r=(std::fmod(rolls[i],360.f)+90*int(side))*PI/180;
    const float p=clamp(pitches[i],-90,90)*PI/180;
    auto& h=horizons_[i];
    h={std::sin(r),std::cos(r),std::sin(p),std::cos(p)};
    style::ladder(*this,p,h.cosine,h.sine);
    style::bank(*this,r);
  }
  if(fpsChanged){
    const auto badge=panelRect({FpsOverlay::X,FpsOverlay::Y,FpsOverlay::Width,FpsOverlay::Height},side);
    add(badge.x,badge.y,badge.x+badge.width-1,badge.y+badge.height-1,0);
  }
}
void RoundDamage::add(float l,float t,float r,float b,float padding){
  const int left=std::max(0,floorBound(l-padding)),right=std::min(N,ceilBound(r+padding)+1);
  const int top=std::max(0,floorBound(t-padding)),bottom=std::min(N,ceilBound(b+padding)+1);
  if(left>=right||top>=bottom)return;
  const uint32_t columns=((uint32_t(1)<<((right-1)/MarkColumns+1))-1)&(~uint32_t(0)<<(left/MarkColumns));
  for(int y=top/MarkRows;y<=(bottom-1)/MarkRows;++y)marks_[y]|=columns;
}
void RoundDamage::line(float x,float y,float xx,float yy,float width,uint16_t){
  const float pad=width*.5f+1;
  if(std::fabs(yy-y)<=MarkRows){
    add(minBound(x,xx),minBound(y,yy),maxBound(x,xx),maxBound(y,yy),pad);
    return;
  }
  const int first=std::max(0,floorBound(minBound(y,yy)-pad))/MarkRows;
  const int last=std::min(N-1,ceilBound(maxBound(y,yy)+pad))/MarkRows;
  const float slope=(xx-x)/(yy-y),low=minBound(x,xx),high=maxBound(x,xx);
  for(int row=first;row<=last;++row){
    const int top=row*MarkRows,bottom=top+MarkRows-1;
    const float a=clamp(x+(top-pad-y)*slope,low,high);
    const float b=clamp(x+(bottom+pad-y)*slope,low,high);
    const int left=std::max(0,floorBound(minBound(a,b)-pad));
    const int right=std::min(N,ceilBound(maxBound(a,b)+pad)+1);
    if(left<right)marks_[row]|=((uint32_t(1)<<((right-1)/MarkColumns+1))-1)&(~uint32_t(0)<<(left/MarkColumns));
  }
}
void RoundDamage::triangle(float x,float y,float xx,float yy,float xxx,float yyy,uint16_t){
  add(std::min({x,xx,xxx}),std::min({y,yy,yyy}),std::max({x,xx,xxx}),std::max({y,yy,yyy}),1.5f);
}
void RoundDamage::pitchDigit(int digit,float x,float y,float cosine,float sine){
  const auto& ink=hudfont::inkBounds[digit];
  constexpr float origin=hudfont::OriginX+hudfont::Advance*.5f;
  const float left=(ink.l-1-origin)*style::PitchDigitScale,right=(ink.r+1-origin)*style::PitchDigitScale;
  const float top=(ink.t-1-hudfont::DigitCenterY)*style::PitchDigitScale;
  const float bottom=(ink.b+1-hudfont::DigitCenterY)*style::PitchDigitScale;
  const float u=(left+right)*.5f,v=(top+bottom)*.5f;
  const float cx=x+u*cosine+v*sine,cy=y-u*sine+v*cosine;
  const float dx=((right-left)*std::fabs(cosine)+(bottom-top)*std::fabs(sine))*.5f;
  const float dy=((right-left)*std::fabs(sine)+(bottom-top)*std::fabs(cosine))*.5f;
  add(cx-dx,cy-dy,cx+dx,cy+dy,1);
}
AirspeedRect RoundDamage::window(AirspeedRect band)const{
  if(!valid_)return band;
  uint32_t needed=0;
  for(int i=band.y/MarkRows;i<=(band.y+band.height-1)/MarkRows;++i)needed|=marks_[i];
  const float y0=band.y-C,y1=band.y+band.height-1-C;
  const float nearY=clamp(0,y0,y1),farY=maxBound(std::fabs(y0),std::fabs(y1));
  const float nearY2=nearY*nearY,farY2=farY*farY;
  // Check both horizon projections across the strip. Inside the ball,
  // bound sphere depth by the nearest and farthest points of the strip.
  // Four pixels leave room for antialiasing and the Q18 raster quantization.
  auto unchanged=[&](int first,int end){
    const float x0=first-C,x1=end-1-C;
    const float nearX=clamp(0,x0,x1),farX=maxBound(std::fabs(x0),std::fabs(x1));
    const float nearR2=nearX*nearX+nearY2,farR2=farX*farX+farY2;
    const bool hasBall=nearR2<BALL*BALL,hasRing=farR2>=BALL*BALL;
    const float deep=hasBall?std::sqrt(BALL*BALL-nearR2):0;
    const float shallow=hasRing?0:std::sqrt(BALL*BALL-farR2);
    bool ringSky=true,ringGround=true,ballSky=true,ballGround=true;
    for(const auto& h:horizons_){
      const float ax=x0*h.sine,bx=x1*h.sine,ay=y0*h.cosine,by=y1*h.cosine;
      const float lo=minBound(ax,bx)+minBound(ay,by),hi=maxBound(ax,bx)+maxBound(ay,by);
      if(hasRing){ringSky&=hi<-4;ringGround&=lo>4;}
      if(hasBall){
        const float firstDepth=-deep*h.pitchSine,lastDepth=-shallow*h.pitchSine;
        const float firstPlane=lo*h.pitchCosine,lastPlane=hi*h.pitchCosine;
        ballSky&=maxBound(firstPlane,lastPlane)+maxBound(firstDepth,lastDepth)<-4;
        ballGround&=minBound(firstPlane,lastPlane)+minBound(firstDepth,lastDepth)>4;
      }
    }
    return (ringSky||ringGround)&&(ballSky||ballGround);
  };
  const int end=band.x+band.width;
  const int firstTile=band.x/MarkColumns,lastTile=(end-1)/MarkColumns;
  int first=firstTile,last=lastTile;
  auto cleanTile=[&](int tile){
    return !(needed&(uint32_t(1)<<tile))&&
      unchanged(std::max(band.x,tile*MarkColumns),std::min(end,(tile+1)*MarkColumns));
  };
  while(first<=last&&cleanTile(first))++first;
  while(last>first&&cleanTile(last))--last;
  if(first>last)return {band.x,band.y,0,band.height};
  const int left=std::max(band.x,first*MarkColumns),right=std::min(end,(last+1)*MarkColumns);
  return {left,band.y,right-left,band.height};
}
Renderer::Renderer():Renderer(false){}
Renderer::Renderer(bool wireOrder):data_(nullptr),wireOrder_(wireOrder){}
Renderer::~Renderer(){release(data_);}
void Renderer::release(Data* data){
  if(!data)return;
  std::free(data->commands);std::free(data->fixedPixels);std::free(data->fixedSpans);std::free(data->fixedColors);
  std::free(data);
}
bool Renderer::begin(){return begin(UsbSide::Left);}
bool Renderer::setInstallation(UsbSide side){
  if(data_&&data_->side==side)return true;
  Data* previous=data_;data_=nullptr;
  if(!begin(side)){data_=previous;return false;}
  release(previous);
#if defined(ARDUINO_ARCH_ESP32)
  // Building the new orientation temporarily retains the previous cache.
  // Recover fast storage after that cache is freed, so orientation changes
  // cannot leave frequently read geometry permanently in external memory.
  auto promote=[](void* source,size_t bytes)->void*{
    if(!source||esp_ptr_internal(source)||!bytes)return source;
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<bytes+12*1024)return source;
    void* fast=heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!fast)return source;
    std::memcpy(fast,source,bytes);std::free(source);return fast;
  };
  data_->commands=static_cast<DrawCommand*>(promote(data_->commands,CommandCapacity*sizeof(DrawCommand)));
  data_->fixedPixels=static_cast<FixedPixel*>(promote(data_->fixedPixels,data_->fixedCount*sizeof(FixedPixel)));
  data_->fixedSpans=static_cast<Data::FixedSpan*>(promote(data_->fixedSpans,data_->fixedSpanCount*sizeof(Data::FixedSpan)));
  const auto& last=data_->fixedSpans[data_->fixedSpanCount-1];
  data_->fixedColors=static_cast<uint16_t*>(promote(data_->fixedColors,(last.offset+last.count)*sizeof(uint16_t)));
  Serial.printf("ROUND cache internal commands=%d edges=%d spans=%d colors=%d\n",
    esp_ptr_internal(data_->commands),esp_ptr_internal(data_->fixedPixels),
    esp_ptr_internal(data_->fixedSpans),esp_ptr_internal(data_->fixedColors));
#endif
  return true;
}
bool Renderer::begin(UsbSide side){
  if(data_)return true;
  data_=static_cast<Data*>(allocate(sizeof(Data)));
  if(!data_)return false;
  data_->side=side;
  data_->commands=static_cast<DrawCommand*>(allocate(CommandCapacity*sizeof(DrawCommand),true));
  if(!data_->commands){std::free(data_);data_=nullptr;return false;}
  // Record static geometry once. Replay preserves the original antialiasing and
  // layer order without repeating distance tests, division or square roots.
  data_->fixedCount=0;data_->fixedPixels=nullptr;
  data_->fixedSpans=nullptr;data_->fixedColors=nullptr;data_->fixedSpanCount=0;
  data_->poseValid=false;
  Canvas counter(nullptr,R,nullptr,&data_->fixedCount);style::fixed(counter);
  data_->fixedPixels=static_cast<FixedPixel*>(allocate(data_->fixedCount*sizeof(FixedPixel)));
  if(!data_->fixedPixels){std::free(data_->commands);std::free(data_);data_=nullptr;return false;}
  size_t recorded=0;
  Canvas recorder(nullptr,R,data_->fixedPixels,&recorded);style::fixed(recorder);
  if(side!=UsbSide::Left){
    for(size_t i=0;i<recorded;++i){
      auto& p=data_->fixedPixels[i];
      const auto physical=toPanel({int(p.index%N),int(p.index/N)},side);
      p.index=physical.y*N+physical.x;
    }
  }
  if(!data_->cacheFixed()){
    std::free(data_->fixedPixels);std::free(data_->commands);std::free(data_);data_=nullptr;return false;
  }
  size_t span=0;
  for(int y=0;y<=N;++y){
    while(span<data_->fixedSpanCount && data_->fixedSpans[span].index<uint32_t(y*N))++span;
    data_->spanRowStart[y]=span;
  }
  const auto fromPanel=inverse(side);
  for(int y=0;y<N;++y)for(int x=0;x<N;++x){
    int i=y*N+x;float dx=x-C,dy=y-C,r2=dx*dx+dy*dy;
    if(r2>R*R){data_->sky[i]=data_->ground[i]=0;data_->depth[i]=-1;continue;}
    float radius=r2<BALL*BALL?BALL:R;
    float depth=std::sqrt(std::fmax(0.f,radius*radius-r2));
    // Bit 14 marks the annulus; sphere depth occupies the lower 14 bits.
    data_->depth[i]=int16_t(depth*64) | (radius==R?0x4000:0);
    const auto logical=toPanel({x,y},fromPanel);
    data_->horizonVisibility[i]=style::background(logical.x,logical.y,data_->sky[i],data_->ground[i]);
  }
  for(int y=0;y<N;++y){
    Data::RowSpan& row=data_->rows[y];row={N,0,N,0};
    for(int x=0;x<N;++x){
      const int d=data_->depth[y*N+x];if(d<0)continue;
      if(row.first==N)row.first=x;row.end=x+1;
      if(!(d&0x4000)){if(row.ballFirst==N)row.ballFirst=x;row.ballEnd=x+1;}
    }
  }
  if(wireOrder_){
    for(int i=0;i<PIXELS;++i){data_->sky[i]=swapBytes(data_->sky[i]);data_->ground[i]=swapBytes(data_->ground[i]);}
    for(size_t i=0;i<data_->fixedSpanCount;++i){
      const Data::FixedSpan& span=data_->fixedSpans[i];
      for(unsigned j=0;j<span.count;++j)data_->fixedColors[span.offset+j]=swapBytes(data_->fixedColors[span.offset+j]);
    }
  }
  return true;
}
void Renderer::render(uint16_t* frame,float pitch,float roll){
  renderBand(frame,pitch,roll,0,N);
}
bool Renderer::hasStableBottom(float pitch,float roll){
  // On rows 400..465 the annulus stays more than seven pixels below the
  // horizon for |roll| <= 44°. Inside the ball, both |dx| and depth are
  // below 48, so it stays below the horizon with |pitch| <= 45° as well.
  // The pitch ladder is clipped to radius 167; the bank marks stay above it.
  return std::fabs(pitch)<=45 && std::fabs(roll)<=44;
}
void Renderer::renderBand(uint16_t* frame,float pitch,float roll,int firstRow,int rowCount){
  if(!frame || firstRow<0 || firstRow>=N || rowCount<=0 || rowCount>N-firstRow)return;
  if(!data_||!std::isfinite(pitch)||!std::isfinite(roll)){std::memset(frame,0,rowCount*N*sizeof(uint16_t));return;}
#if defined(ARDUINO_ARCH_ESP32)
  const int64_t stamp=esp_timer_get_time();
#endif
  renderBackgroundBand(frame,pitch,roll,firstRow,rowCount);
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[0]=esp_timer_get_time()-stamp;
#endif
  renderForegroundBand(frame,pitch,roll,firstRow,rowCount);
}
void Renderer::renderBackgroundBand(uint16_t* frame,float pitch,float roll,int firstRow,int rowCount,bool compactCircle){
  if(!frame || firstRow<0 || firstRow>=N || rowCount<=0 || rowCount>N-firstRow)return;
  const CircleBand band=compactCircle?circleBandBounds(firstRow,rowCount):CircleBand{0,N};
  renderBackgroundWindow(frame,pitch,roll,{band.x,firstRow,band.width,rowCount});
}
void ROUND_HOT Renderer::renderBackgroundWindow(uint16_t* frame,float pitch,float roll,AirspeedRect rect){
  const int firstRow=rect.y,rowCount=rect.height;
  const CircleBand band{rect.x,rect.width};
  if(!frame||firstRow<0||firstRow>=N||rowCount<=0||rowCount>N-firstRow||
     band.x<0||band.x>=N||band.width<=0||band.width>N-band.x)return;
  const Data* data=data_;
  if(!data||!std::isfinite(pitch)||!std::isfinite(roll)){std::memset(frame,0,rowCount*band.width*sizeof(uint16_t));return;}
  pitch=clamp(pitch,-90,90)*PI/180;roll=(std::fmod(roll,360.f)+90*int(data->side))*PI/180;
  float cp=std::cos(pitch),sp=std::sin(pitch),cr=std::cos(roll),sr=std::sin(roll);
  // Q18 screen coordinates avoid floating-point work for every background pixel.
  // Products stay below 2^28; depth uses Q6, hence the Q12 depth coefficient.
  const int32_t scale=1<<18;
  const int32_t outerStep=roundBound(sr*scale),innerStep=roundBound(sr*cp*scale);
  const int32_t depthStep=roundBound(sp*4096.f);
  const float a=sr*cp,q=a*a+sp*sp;
  for(int y=firstRow;y<firstRow+rowCount;++y){
    float rotated=(y-C)*cr-C*sr;
    int32_t outer=roundBound(rotated*scale),inner=roundBound(rotated*cp*scale);
    Data::RowSpan row=data->rows[y];
    row.first=std::max(int(row.first),band.x);
    row.end=std::min(int(row.end),band.x+band.width);
    if(row.ballEnd){
      row.ballFirst=std::max(row.ballFirst,row.first);
      row.ballEnd=std::min(row.ballEnd,row.end);
      if(row.ballEnd<=row.ballFirst)row.ballEnd=0;
    }
    uint16_t* out=frame+(y-firstRow)*band.width;
    // Pole rows have no visible pixels; their sentinel span is [N,0).
    // Handle it before converting the nonempty global span to compact indices.
    if(row.first>=row.end){std::memset(out,0,band.width*sizeof(uint16_t));continue;}
    std::memset(out,0,(row.first-band.x)*sizeof(uint16_t));
    std::memset(out+(row.end-band.x),0,(band.x+band.width-row.end)*sizeof(uint16_t));
    BackgroundRow shader={out,data->sky+y*N,data->ground+y*N,data->depth+y*N,data->horizonVisibility+y*N,outer,outerStep,depthStep,band.x,false,wireOrder_};
    // Bound elevation at the span endpoints. If both the annulus and the
    // sphere are beyond the horizon's guard on the same side, one copy can
    // cover the entire row. Bound the sphere displacement independently of x.
    const int32_t a0=outer+row.first*outerStep,a1=outer+(row.end-1)*outerStep;
    const int32_t near=std::abs(a0)<std::abs(a1)?a0:a1;
    if((a0<0)==(a1<0) && std::abs(near)>=style::HorizonGuardQ18){
      const int32_t b0=inner+row.ballFirst*innerStep,b1=inner+(row.ballEnd-1)*innerStep;
      const int32_t depthBound=int32_t(BALL*64)*depthStep;
      const bool sky=near<0;
      const int32_t sphereNear=sky?(b0>b1?b0:b1)-(depthBound<0?depthBound:0):
                                   (b0<b1?b0:b1)-(depthBound>0?depthBound:0);
      if(!row.ballEnd || (sky?-sphereNear:sphereNear)>=style::HorizonGuardQ18){
        copySpan(out+row.first-band.x,(sky?shader.sky:shader.ground)+row.first,row.end-row.first);
        continue;
      }
    }
    float roots[4];int rootCount=0;
    const float dy=y-C;
    if(std::fabs(sr)>1.e-6f){
      roots[rootCount++]=(-style::HorizonGuard-cr*dy)/sr;
      roots[rootCount++]=(style::HorizonGuard-cr*dy)/sr;
    }
    if(row.ballEnd==0){shader.spans(row.first,row.end,roots,rootCount);continue;}
    shader.spans(row.first,row.ballFirst,roots,rootCount);
    shader.spans(row.ballEnd,row.end,roots,rootCount);
    rootCount=0;
    if(q>1.e-12f){
      const float r2=BALL*BALL-dy*dy;
      for(int sign=-1;sign<=1;sign+=2){
        const float k=cr*cp*dy-sign*style::HorizonGuard,discriminant=q*r2-k*k;
        if(discriminant<0)continue;
        const float radical=std::fabs(sp)*std::sqrt(discriminant);
        // Squaring can introduce extraneous roots, which only adds a few
        // conservative shader pixels and never changes the selected color.
        roots[rootCount++]=(-a*k-radical)/q;
        roots[rootCount++]=(-a*k+radical)/q;
      }
    }
    shader.base=inner;shader.step=innerStep;shader.inner=true;
    shader.spans(row.ballFirst,row.ballEnd,roots,rootCount);
  }
}
void Renderer::renderForegroundBand(uint16_t* frame,float pitch,float roll,int firstRow,int rowCount,bool compactCircle){
  if(!frame || firstRow<0 || firstRow>=N || rowCount<=0 || rowCount>N-firstRow)return;
  const CircleBand band=compactCircle?circleBandBounds(firstRow,rowCount):CircleBand{0,N};
  renderForegroundWindow(frame,pitch,roll,{band.x,firstRow,band.width,rowCount});
}
void Renderer::renderForegroundWindow(uint16_t* frame,float pitch,float roll,AirspeedRect rect){
  if(!frame||rect.y<0||rect.y>=N||rect.height<=0||rect.height>N-rect.y||
     rect.x<0||rect.x>=N||rect.width<=0||rect.width>N-rect.x||
     !std::isfinite(pitch)||!std::isfinite(roll))return;
  prepareForeground(pitch,roll);
  renderPreparedForegroundWindow(frame,rect,true);
}
void Renderer::prepareForeground(float pitch,float roll){
  if(!data_||!std::isfinite(pitch)||!std::isfinite(roll))return;
  if(data_->poseValid&&data_->posePitch==pitch&&data_->poseRoll==roll)return;
  data_->posePitch=pitch;data_->poseRoll=roll;data_->poseValid=true;
  pitch=clamp(pitch,-90,90)*PI/180;roll=(std::fmod(roll,360.f)+90*int(data_->side))*PI/180;
  Canvas recorder(nullptr,BALL-7);
  recorder.commands=data_->commands;recorder.commandCount=&data_->commandCount;
  recorder.commandOverflow=&data_->poseOverflow;
  data_->commandCount=0;data_->poseOverflow=false;
  style::ladder(recorder,pitch,std::cos(roll),std::sin(roll));data_->bankStart=data_->commandCount;
#if defined(INSTRUMENT_TEST_POSE_CACHE)
  const bool ladderOverflow=data_->poseOverflow;
#endif
  style::bank(recorder,roll);
  if(!data_->poseOverflow){
    std::memset(data_->commandBands,0,sizeof(data_->commandBands));
    for(size_t i=0;i<data_->commandCount;++i){
      const auto& p=data_->commands[i];
      float lo,hi;
      if(p.kind==DrawCommand::Line){
        const float pad=p.width*.5f+1;
        lo=minBound(p.y1,p.y2)-pad;hi=maxBound(p.y1,p.y2)+pad;
      }else if(p.kind==DrawCommand::Triangle){
        lo=std::min({p.y1,p.y2,p.y3})-1.5f;hi=std::max({p.y1,p.y2,p.y3})+1.5f;
      }else{
        const auto& ink=hudfont::inkBounds[p.digit];
        constexpr float origin=hudfont::OriginX+hudfont::Advance*.5f;
        const float dx=std::max(std::fabs(ink.l-1-origin),std::fabs(ink.r+1-origin));
        const float dy=std::max(std::fabs(ink.t-1-hudfont::DigitCenterY),std::fabs(ink.b+1-hudfont::DigitCenterY));
        const float pad=(dx*std::fabs(p.y2)+dy*std::fabs(p.x2))*style::PitchDigitScale+1;
        lo=p.y1-pad;hi=p.y1+pad;
      }
      const int first=std::max(0,floorBound(lo))/CommandBandRows;
      const int last=std::min(N-1,ceilBound(hi))/CommandBandRows;
      for(int y=first;y<=last;++y)data_->commandBands[y][i/32]|=uint32_t(1)<<(i%32);
    }
  }
#if defined(INSTRUMENT_TEST_POSE_CACHE)
  ++backgroundPoseBuilds;backgroundPoseCommands=data_->commandCount;
  if(data_->poseOverflow)++backgroundPoseOverflows;
  if(data_->poseOverflow && !ladderOverflow)++backgroundPoseBankOverflows;
#endif
}
void ROUND_HOT Renderer::renderPreparedForegroundWindow(uint16_t* frame,AirspeedRect rect,bool timings) const{
  const int firstRow=rect.y,rowCount=rect.height;
  const CircleBand band{rect.x,rect.width};
  if(!frame||firstRow<0||firstRow>=N||rowCount<=0||rowCount>N-firstRow||
     band.x<0||band.x>=N||band.width<=0||band.width>N-band.x||
     !data_||!data_->poseValid)return;
#if defined(ARDUINO_ARCH_ESP32)
  int64_t stamp=timings?esp_timer_get_time():0;
#else
  (void)timings;
#endif
  Canvas c={frame,BALL-7,nullptr,nullptr,firstRow,firstRow+rowCount,wireOrder_,band.x,band.width};
  uint32_t visible[CommandWords]={};
  if(!data_->poseOverflow){
    const int first=firstRow/CommandBandRows,last=(firstRow+rowCount-1)/CommandBandRows;
    for(int y=first;y<=last;++y)
      for(size_t i=0;i<CommandWords;++i)visible[i]|=data_->commandBands[y][i];
  }
  float roll=0;
  if(data_->poseOverflow){
    const float pitch=clamp(data_->posePitch,-90,90)*PI/180;
    roll=(std::fmod(data_->poseRoll,360.f)+90*int(data_->side))*PI/180;
    style::ladder(c,pitch,std::cos(roll),std::sin(roll));
  }else c.replay(data_->commands,visible,0,data_->bankStart);
#if defined(ARDUINO_ARCH_ESP32)
  if(timings){renderStages[1]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();}
#endif
  if(data_->poseOverflow){c.limit=R;style::bank(c,roll);}
  else c.replay(data_->commands,visible,data_->bankStart,data_->commandCount);
#if defined(ARDUINO_ARCH_ESP32)
  if(timings){renderStages[2]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();}
#endif
  // Opaque runs and retained edge operations affect disjoint pixels.
  const uint32_t bandFirst=firstRow*N,bandEnd=(firstRow+rowCount)*N;
  // Fixed spans are sorted by row and never cross a row boundary.
  for(size_t i=data_->spanRowStart[firstRow];i<data_->spanRowStart[firstRow+rowCount];++i){
    const Data::FixedSpan& span=data_->fixedSpans[i];
    const uint32_t first=span.index>bandFirst?span.index:bandFirst;
    const uint32_t end=span.index+span.count<bandEnd?span.index+span.count:bandEnd;
    if(first>=end)continue;
    if(band.width==N){
      copySpan(frame+(first-bandFirst),data_->fixedColors+span.offset+(first-span.index),end-first);
      continue;
    }
    const uint32_t row=span.index/N,left=row*N+band.x,right=left+band.width;
    const uint32_t lo=std::max(first,left),hi=std::min(end,right);
    if(lo<hi)copySpan(frame+((row-firstRow)*band.width+lo-left),data_->fixedColors+span.offset+(lo-span.index),hi-lo);
  }
  const size_t edgeEnd=data_->fixedRowStart[firstRow+rowCount];
  for(size_t i=data_->fixedRowStart[firstRow];i<edgeEnd;++i){
    const FixedPixel& p=data_->fixedPixels[i];
    uint32_t index=p.index-bandFirst;
    if(band.width!=N){
      const int column=p.index%N;
      if(column<band.x || column>=band.x+band.width)continue;
      index=(p.index/N-firstRow)*band.width+column-band.x;
    }
    if(wireOrder_)frame[index]=swapBytes(p.alpha>=1?p.color:blend(swapBytes(frame[index]),p.color,p.alpha));
    else frame[index]=p.alpha>=1?p.color:blend(frame[index],p.color,p.alpha);
  }
#if defined(ARDUINO_ARCH_ESP32)
  if(timings)renderStages[3]=esp_timer_get_time()-stamp;
#endif
}
}
