#include "renderer.h"
#include "instrument_style.h"
#include "circle_band.h"
#include "raster_math.h"
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
struct DrawCommand {
  float x1,y1,x2,y2,x3,y3,width,limit;
  uint16_t color;bool triangle;
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
#define INSTRUMENT_COMMAND_CAPACITY 1024
#endif
const size_t CommandCapacity=INSTRUMENT_COMMAND_CAPACITY;
const int N=466, PIXELS=N*N;
const float C=232.5f, R=232.5f, BALL=174.0f, PI=3.14159265358979323846f;
const uint16_t WHITE=0xffde;
uint16_t swapBytes(uint16_t color){return static_cast<uint16_t>((color<<8)|(color>>8));}
float clamp(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
uint16_t blend(uint16_t a,uint16_t b,float t){
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
  void pixel(int x,int y,uint16_t color,float alpha){
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
  void line(float x1,float y1,float x2,float y2,float width,uint16_t col){
    if(commandCount){
      if(*commandCount<CommandCapacity)commands[(*commandCount)++]={x1,y1,x2,y2,0,0,width,limit,col,false};
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
    const float inverseLength=len>0?1.f/len:0;
    const float inverseDistance=len>0?1.f/std::sqrt(len):0;
    const bool clipStrip=!count && std::fabs(vy)>.2f*std::fabs(vx);
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
      for(int x=first;x<=last;++x){
        const float projection=(x-x1)*vx+(y-y1)*vy;
        if(!count && len>0 && projection>=0 && projection<=len){
          const float distance=std::fabs((x-x1)*vy-(y-y1)*vx)*inverseDistance;
          if(distance<=half-.5f)pixel(x,y,col,1);
          else if(distance<half+.5f)pixel(x,y,col,half+.5f-distance);
          continue;
        }
        // Round caps and one-time fixed geometry recording retain the original
        // distance calculation. Segment interiors need no per-pixel square root.
        float t=len>0?clamp(projection*inverseLength,0,1):0;
        float dx=x-x1-t*vx,dy=y-y1-t*vy;
        float d2=dx*dx+dy*dy;
        if(half>=.5f && d2<=(half-.5f)*(half-.5f))pixel(x,y,col,1);
        else if(d2<(half+.5f)*(half+.5f))pixel(x,y,col,half+.5f-std::sqrt(d2));
      }
    }
  }
  void triangle(float ax,float ay,float bx,float by,float cx,float cy,uint16_t color){
    if(commandCount){
      if(*commandCount<CommandCapacity)commands[(*commandCount)++]={ax,ay,bx,by,cx,cy,1,limit,color,true};
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
  void replay(const DrawCommand* commands,size_t first,size_t end){
    for(size_t i=first;i<end;++i){
      const DrawCommand& p=commands[i];limit=p.limit;
      if(p.triangle)triangle(p.x1,p.y1,p.x2,p.y2,p.x3,p.y3,p.color);
      else line(p.x1,p.y1,p.x2,p.y2,p.width,p.color);
    }
  }
};
void bank(Canvas& c,float roll){
  c.limit=R;
  const int angles[]={-60,-45,-30,-20,-10,10,20,30,45,60};
  for(unsigned i=0;i<sizeof(angles)/sizeof(angles[0]);++i){
    float a=angles[i]*PI/180-roll,sa=std::sin(a),ca=std::cos(a);
    float inner=181,outer=(std::abs(angles[i])==30||std::abs(angles[i])==60)?219:207;
    if(std::abs(angles[i])==45){
      float tx=C+181*sa,ty=C-181*ca;
      c.triangle(tx,ty,C+204*sa-7*ca,C-204*ca-7*sa,C+204*sa+7*ca,C-204*ca+7*sa,WHITE);
    }else c.line(C+inner*sa,C-inner*ca,C+outer*sa,C-outer*ca,3,WHITE);
  }
  float cr=std::cos(roll),sr=std::sin(roll);
  c.triangle(C-177.5f*sr,C-177.5f*cr,
             C-8*cr-218.5f*sr,C+8*sr-218.5f*cr,
             C+8*cr-218.5f*sr,C-8*sr-218.5f*cr,WHITE);
}
void fixed(Canvas& c){style::fixed(c);}
// Compact single-stroke numerals: quiet instrument lettering, rotated with the ball.
const float zero[]={0.2f,0, .8f,0, 1,.2f, 1,.8f, .8f,1, .2f,1, 0,.8f, 0,.2f, .2f,0};
const float one[]={.15f,.2f, .55f,0, .55f,1};
const float two[]={0,.2f, .2f,0, .8f,0, 1,.2f, .9f,.4f, 0,1, 1,1};
const float three[]={0,0, 1,0, .5f,.45f, .9f,.55f, 1,.8f, .8f,1, 0,1};
const float four[]={.8f,1, .8f,0, 0,.7f, 1,.7f};
const float five[]={1,0, 0,0, 0,.45f, .8f,.45f, 1,.65f, 1,.85f, .8f,1, 0,1};
const float six[]={.9f,0, .2f,.1f, 0,.5f, 0,.8f, .2f,1, .8f,1, 1,.8f, 1,.6f, .8f,.45f, 0,.45f};
const float seven[]={0,0, 1,0, .3f,1};
const float eight[]={.2f,0, .8f,0, 1,.2f, .8f,.45f, .2f,.55f, 0,.8f, .2f,1, .8f,1, 1,.8f, .8f,.55f, .2f,.45f, 0,.2f, .2f,0};
const float nine[]={1,.55f, .2f,.55f, 0,.35f, 0,.15f, .2f,0, .8f,0, 1,.2f, 1,.7f, .8f,1, .1f,1};
void digit(Canvas& c,int d,float x,float y,float cr,float sr){
  const float* paths[]={zero,one,two,three,four,five,six,seven,eight,nine};
  const int sizes[]={9,3,7,7,4,8,10,3,13,10};
  const float* p=paths[d];
  for(int i=1;i<sizes[d];++i){
    float x1=x+p[2*i-2]*7,y1=y+p[2*i-1]*13,x2=x+p[2*i]*7,y2=y+p[2*i+1]*13;
    c.line(C+x1*cr+y1*sr,C-x1*sr+y1*cr,C+x2*cr+y2*sr,C-x2*sr+y2*cr,1.5f,WHITE);
  }
}
void ladder(Canvas& c,float pitch,float cr,float sr){
  for(int latitude=-80;latitude<=80;latitude+=5){
    if(latitude==0)continue;
    float angle=latitude*PI/180-pitch;
    if(std::cos(angle)<.15f || std::fabs(angle)>40*PI/180)continue;
    float sine=std::sin(angle);
    bool major=latitude%10==0;
    float half=major?(std::abs(latitude)%20==0?57:40):17;
    float py=-std::sqrt(BALL*BALL-half*half)*sine,px=-half;
    for(float x=-half+4;x<=half+3.9f;x+=4){
      float next=std::fmin(x,half),y=-std::sqrt(BALL*BALL-next*next)*sine;
      c.line(C+px*cr+py*sr,C-px*sr+py*cr,C+next*cr+y*sr,C-next*sr+y*cr,major?2:1.6f,WHITE);px=next;py=y;
    }
    if(major){
      float yy=-std::sqrt(BALL*BALL-(half+17)*(half+17))*sine-6.5f;
      int num=std::abs(latitude);
      digit(c,num/10,-half-25,yy,cr,sr);digit(c,0,-half-15,yy,cr,sr);
      digit(c,num/10,half+9,yy,cr,sr);digit(c,0,half+19,yy,cr,sr);
    }
  }
}
// Copy complete sky/ground spans as aligned words on ESP32. Both pointers may
// be only RGB565-aligned, so mismatched word alignment uses the portable copy.
void copySpan(uint16_t* dst,const uint16_t* src,int count){
#if defined(__GNUC__) && (defined(ARDUINO_ARCH_ESP32) || defined(INSTRUMENT_TEST_WORD_COPY))
  typedef uint32_t Word __attribute__((__may_alias__));
  if((reinterpret_cast<uintptr_t>(dst)^reinterpret_cast<uintptr_t>(src))&3){
    std::memcpy(dst,src,count*sizeof(uint16_t));return;
  }
  if(count && (reinterpret_cast<uintptr_t>(dst)&3)){*dst++=*src++;--count;}
  Word* out=reinterpret_cast<Word*>(dst);
  const Word* in=reinterpret_cast<const Word*>(src);
  for(int words=count/2;words;--words)*out++=*in++;
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
  void shade(int first,int end)const{
    int32_t plane=base+first*step;
    for(int x=first;x<end;++x,plane+=step){
      const int32_t e=plane-(inner?depth[x]*depthStep:0);
      const uint16_t s=sky[x],g=ground[x];
      uint16_t color=e<0?s:g;
      const int32_t distance=std::abs(e);
      if(distance<314573 && s!=g){
        const float alpha=clamp(1.2f-distance*(1.f/262144.f),0,1)*(horizonVisibility[x]*(1.f/255.f));
        color=wireOrder?swapBytes(blend(swapBytes(color),WHITE,alpha)):blend(color,WHITE,alpha);
      }
      frame[x-firstColumn]=color;
    }
  }
  void spans(int first,int end,const float* roots,int rootCount)const{
    // Up to four algebraic crossings. A three-pixel window absorbs floating
    // point root rounding; the wider 1.5 threshold also covers Q6/Q18 error.
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
      if(edge || std::abs(e)<393216)shade(lo,hi);
      else copySpan(frame+(lo-firstColumn),(e<0?sky:ground)+lo,hi-lo);
    }
  }
};
}
struct Renderer::Data {
  uint16_t sky[PIXELS],ground[PIXELS]; int16_t depth[PIXELS];
  uint8_t horizonVisibility[PIXELS];
  struct RowSpan {int16_t first,end,ballFirst,ballEnd;} rows[N];
  FixedPixel* fixedPixels; size_t fixedCount;
  uint32_t fixedRowStart[N+1];
  struct FixedSpan {uint32_t index,offset;uint16_t count;};
  FixedSpan* fixedSpans;uint16_t* fixedColors;size_t fixedSpanCount;
  DrawCommand* commands;
  size_t commandCount,bankStart;
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
      const int first=i;while(i<PIXELS && ground[i])++i;
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
      const int first=i;while(i<PIXELS && ground[i])++i;
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
Renderer::Renderer():Renderer(false){}
Renderer::Renderer(bool wireOrder):data_(nullptr),wireOrder_(wireOrder){}
Renderer::~Renderer(){
  if(data_){std::free(data_->commands);std::free(data_->fixedPixels);std::free(data_->fixedSpans);std::free(data_->fixedColors);}
  std::free(data_);
}
bool Renderer::begin(){
  if(data_)return true;
  data_=static_cast<Data*>(allocate(sizeof(Data)));
  if(!data_)return false;
  data_->commands=static_cast<DrawCommand*>(allocate(CommandCapacity*sizeof(DrawCommand),true));
  if(!data_->commands){std::free(data_);data_=nullptr;return false;}
  // Record static geometry once. Replay preserves the original antialiasing and
  // layer order without repeating distance tests, division or square roots.
  data_->fixedCount=0;data_->fixedPixels=nullptr;
  data_->fixedSpans=nullptr;data_->fixedColors=nullptr;data_->fixedSpanCount=0;
  data_->poseValid=false;
  Canvas counter(nullptr,R,nullptr,&data_->fixedCount);fixed(counter);
  data_->fixedPixels=static_cast<FixedPixel*>(allocate(data_->fixedCount*sizeof(FixedPixel)));
  if(!data_->fixedPixels){std::free(data_->commands);std::free(data_);data_=nullptr;return false;}
  size_t recorded=0;
  Canvas recorder(nullptr,R,data_->fixedPixels,&recorded);fixed(recorder);
  if(!data_->cacheFixed()){
    std::free(data_->fixedPixels);std::free(data_->commands);std::free(data_);data_=nullptr;return false;
  }
  for(int y=0;y<N;++y)for(int x=0;x<N;++x){
    int i=y*N+x;float dx=x-C,dy=y-C,r2=dx*dx+dy*dy;
    if(r2>R*R){data_->sky[i]=data_->ground[i]=0;data_->depth[i]=-1;continue;}
    float radius=r2<BALL*BALL?BALL:R;
    float depth=std::sqrt(std::fmax(0.f,radius*radius-r2));
    // Bit 14 marks the annulus; sphere depth occupies the lower 14 bits.
    data_->depth[i]=int16_t(depth*64) | (radius==R?0x4000:0);
    data_->horizonVisibility[i]=style::background(x,y,data_->sky[i],data_->ground[i]);
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
  const Data* data=data_;
  if(!data||!std::isfinite(pitch)||!std::isfinite(roll)){std::memset(frame,0,rowCount*band.width*sizeof(uint16_t));return;}
  pitch=clamp(pitch,-90,90)*PI/180;roll=std::fmod(roll,360.f)*PI/180;
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
    const Data::RowSpan row=data->rows[y];
    uint16_t* out=frame+(y-firstRow)*band.width;
    // Pole rows have no visible pixels; their sentinel span is [N,0).
    // Handle it before converting the nonempty global span to compact indices.
    if(row.first>=row.end){std::memset(out,0,band.width*sizeof(uint16_t));continue;}
    std::memset(out,0,(row.first-band.x)*sizeof(uint16_t));
    std::memset(out+(row.end-band.x),0,(band.x+band.width-row.end)*sizeof(uint16_t));
    BackgroundRow shader={out,data->sky+y*N,data->ground+y*N,data->depth+y*N,data->horizonVisibility+y*N,outer,outerStep,depthStep,band.x,false,wireOrder_};
    float roots[4];int rootCount=0;
    const float dy=y-C;
    if(std::fabs(sr)>1.e-6f){
      roots[rootCount++]=(-1.5f-cr*dy)/sr;
      roots[rootCount++]=(1.5f-cr*dy)/sr;
    }
    if(row.ballEnd==0){shader.spans(row.first,row.end,roots,rootCount);continue;}
    shader.spans(row.first,row.ballFirst,roots,rootCount);
    shader.spans(row.ballEnd,row.end,roots,rootCount);
    rootCount=0;
    if(q>1.e-12f){
      const float r2=BALL*BALL-dy*dy;
      for(int sign=-1;sign<=1;sign+=2){
        const float k=cr*cp*dy-sign*1.5f,discriminant=q*r2-k*k;
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
  if(!data_||!std::isfinite(pitch)||!std::isfinite(roll))return;
  const CircleBand band=compactCircle?circleBandBounds(firstRow,rowCount):CircleBand{0,N};
  const float inputPitch=pitch,inputRoll=roll;
  pitch=clamp(pitch,-90,90)*PI/180;roll=std::fmod(roll,360.f)*PI/180;
  float cr=std::cos(roll),sr=std::sin(roll);
#if defined(ARDUINO_ARCH_ESP32)
  int64_t stamp=esp_timer_get_time();
#endif
  Canvas c={frame,BALL-7,nullptr,nullptr,firstRow,firstRow+rowCount,wireOrder_,band.x,band.width};
  const bool prepare=!data_->poseValid || data_->posePitch!=inputPitch || data_->poseRoll!=inputRoll;
  Canvas recorder(nullptr,BALL-7);
  recorder.commands=data_->commands;recorder.commandCount=&data_->commandCount;
  recorder.commandOverflow=&data_->poseOverflow;
  if(prepare){
    data_->commandCount=0;data_->poseOverflow=false;
    ladder(recorder,pitch,cr,sr);data_->bankStart=data_->commandCount;
  }
  if(data_->poseOverflow)ladder(c,pitch,cr,sr);
  else c.replay(data_->commands,0,data_->bankStart);
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[1]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();
#endif
  if(prepare){
#if defined(INSTRUMENT_TEST_POSE_CACHE)
    const bool ladderOverflow=data_->poseOverflow;
#endif
    bank(recorder,roll);
    data_->posePitch=inputPitch;data_->poseRoll=inputRoll;data_->poseValid=true;
#if defined(INSTRUMENT_TEST_POSE_CACHE)
    ++backgroundPoseBuilds;backgroundPoseCommands=data_->commandCount;
    if(data_->poseOverflow)++backgroundPoseOverflows;
    if(data_->poseOverflow && !ladderOverflow)++backgroundPoseBankOverflows;
#endif
  }
  if(data_->poseOverflow){c.limit=R;bank(c,roll);}
  else c.replay(data_->commands,data_->bankStart,data_->commandCount);
#if defined(ARDUINO_ARCH_ESP32)
  renderStages[2]=esp_timer_get_time()-stamp;stamp=esp_timer_get_time();
#endif
  // Opaque runs and retained edge operations affect disjoint pixels.
  const uint32_t bandFirst=firstRow*N,bandEnd=(firstRow+rowCount)*N;
  for(size_t i=0;i<data_->fixedSpanCount;++i){
    const Data::FixedSpan& span=data_->fixedSpans[i];
    const uint32_t first=span.index>bandFirst?span.index:bandFirst;
    const uint32_t end=span.index+span.count<bandEnd?span.index+span.count:bandEnd;
    if(first>=end)continue;
    if(band.width==N){
      copySpan(frame+(first-bandFirst),data_->fixedColors+span.offset+(first-span.index),end-first);
      continue;
    }
    // Cached opaque spans can cross global row boundaries. Split and intersect
    // each row before translating to the compact destination's stride.
    uint32_t rowStart=(first/N)*N;
    uint32_t targetRow=(first/N-firstRow)*band.width;
    while(rowStart<end){
      const uint32_t left=rowStart+band.x,right=left+band.width;
      const uint32_t lo=first>left?first:left,hi=end<right?end:right;
      if(lo<hi)copySpan(frame+(targetRow+lo-left),data_->fixedColors+span.offset+(lo-span.index),hi-lo);
      rowStart+=N;targetRow+=band.width;
    }
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
  renderStages[3]=esp_timer_get_time()-stamp;
#endif
}
}
