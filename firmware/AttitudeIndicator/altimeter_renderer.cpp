#include "altimeter_renderer.h"
#include "altimeter_scale.h"
#include "dial_art.h"
#include "raster_math.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
namespace instrument {
namespace altimeter_detail {
using dialart::Point;
constexpr int N=466;
constexpr float C=232.5f,PI=3.14159265358979323846f;
constexpr uint16_t Cream=0xffda;
inline float clamp(float v,float lo,float hi){return std::max(lo,std::min(hi,v));}
inline uint16_t swap(uint16_t c){return static_cast<uint16_t>((c<<8)|(c>>8));}
inline uint16_t rgb(int r,int g,int b){return static_cast<uint16_t>((r>>3)<<11|(g>>2)<<5|(b>>3));}
inline __attribute__((always_inline)) uint16_t blend(uint16_t a,uint16_t b,unsigned alpha){
  if(alpha==255)return b;
  const unsigned t=alpha+(alpha>=128),u=256-t;
  return static_cast<uint16_t>(((((a>>11)*u+(b>>11)*t+128)>>8)<<11)|
    (((((a>>5)&63)*u+((b>>5)&63)*t+128)>>8)<<5)|(((a&31)*u+(b&31)*t+128)>>8));
}
void* memory(size_t bytes){
#if defined(ARDUINO_ARCH_ESP32)
  return heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  return std::malloc(bytes);
#endif
}
bool valid(uint16_t* p,AirspeedRect r){return p&&r.x>=0&&r.y>=0&&r.x<N&&r.y<N&&r.width>0&&r.height>0&&r.width<=N-r.x&&r.height<=N-r.y;}
float angle(float altitude,int hand){
  const float revolution=hand==0?altitude_scale::LongRevolutionM:hand==1?altitude_scale::ShortRevolutionM:altitude_scale::TriangleRevolutionM;
  return std::fmod(clamp(altitude,0,altitude_scale::MaxM),revolution)*(2*PI/revolution);
}
Point transform(Point p,float cs,float sn){return {C+p.x*cs-p.y*sn,C+p.x*sn+p.y*cs};}
struct Polygon {Point p[20]={};float slope[20]={};int count=0;float minX=1000,maxX=-1000,minY=1000,maxY=-1000;};
void slopes(Polygon& p){
  p.minX=p.minY=1000;p.maxX=p.maxY=-1000;
  for(int i=0,j=p.count-1;i<p.count;j=i++){
    p.slope[i]=p.p[j].y==p.p[i].y?0:(p.p[j].x-p.p[i].x)/(p.p[j].y-p.p[i].y);
    p.minX=std::min(p.minX,p.p[i].x);p.maxX=std::max(p.maxX,p.p[i].x);p.minY=std::min(p.minY,p.p[i].y);p.maxY=std::max(p.maxY,p.p[i].y);
  }
}
bool overlaps(const Polygon& p,AirspeedRect r){return p.maxX+.5f>=r.x&&p.minX-.5f<r.x+r.width&&p.maxY+.5f>=r.y&&p.minY-.5f<r.y+r.height;}

Polygon polygon(const Point* points,int count,float a){
  Polygon p;p.count=count;const float cs=std::cos(a),sn=std::sin(a);
  for(int i=0;i<count;++i)p.p[i]=transform(points[i],cs,sn);
  slopes(p);return p;
}
// Convex scanlines: only the few boundary pixels need subpixel arithmetic.
// Opaque interiors are contiguous fills; there is no per-pixel trig or sqrt.
void fill(uint16_t* pixels,AirspeedRect rect,const Polygon& source,uint16_t color,unsigned opacity,bool wire){
  if(!overlaps(source,rect))return;
  const Polygon p=source;
  const float top=p.minY,bottom=p.maxY;
  int y0=std::max(rect.y,static_cast<int>(floorBound(top-.5f))),y1=std::min(rect.y+rect.height-1,static_cast<int>(ceilBound(bottom+.5f)));
  const uint16_t encoded=wire?swap(color):color;
  for(int y=y0;y<=y1;++y){
    float left[4],right[4],first=1000,last=-1000,solidLeft=-1000,solidRight=1000;
    for(int s=0;s<4;++s){
      const float scan=y+(s+.5f)*.25f-.5f;left[s]=1000;right[s]=-1000;
      for(int i=0,j=p.count-1;i<p.count;j=i++)if((p.p[i].y>scan)!=(p.p[j].y>scan)){
        const float x=p.p[i].x+(scan-p.p[i].y)*p.slope[i];
        left[s]=std::min(left[s],x);right[s]=std::max(right[s],x);
      }
      first=std::min(first,left[s]);last=std::max(last,right[s]);
      solidLeft=std::max(solidLeft,left[s]);solidRight=std::min(solidRight,right[s]);
    }
    const int begin=std::max(rect.x,static_cast<int>(floorBound(first))),end=std::min(rect.x+rect.width-1,static_cast<int>(ceilBound(last)));
    const int solidBegin=std::max(begin,static_cast<int>(ceilBound(solidLeft+.5f))),solidEnd=std::min(end,static_cast<int>(floorBound(solidRight-.5f)));
    uint16_t* row=pixels+(y-rect.y)*rect.width;
    for(int x=begin;x<=end;){
      if(x==solidBegin&&solidEnd>=solidBegin){
        if(opacity==255)std::fill(row+x-rect.x,row+solidEnd+1-rect.x,encoded);
        else for(int col=x;col<=solidEnd;++col){uint16_t c=wire?swap(row[col-rect.x]):row[col-rect.x];c=blend(c,color,opacity);row[col-rect.x]=wire?swap(c):c;}
        x=solidEnd+1;continue;
      }
      float coverage=0;
      for(int s=0;s<4;++s)coverage+=clamp(std::min(x+.5f,right[s])-std::max(x-.5f,left[s]),0,1);
      unsigned alpha=static_cast<unsigned>(coverage*(opacity*.25f)+.5f);
      if(alpha){uint16_t c=wire?swap(row[x-rect.x]):row[x-rect.x];c=blend(c,color,alpha);row[x-rect.x]=wire?swap(c):c;}
      ++x;
    }
  }
}
// Merge all three soft-shadow layers in a small internal scanline. Only one
// RGB565 read/write touches PSRAM per covered pixel instead of three passes.
void shadowGroup(uint16_t* pixels,AirspeedRect rect,const Polygon* source,bool wire){
  if(!overlaps(source[0],rect))return;
  const Polygon p[3]={source[0],source[1],source[2]};
  const float top=p[0].minY,bottom=p[0].maxY;
  const int y0=std::max(rect.y,static_cast<int>(floorBound(top-.5f))),y1=std::min(rect.y+rect.height-1,static_cast<int>(ceilBound(bottom+.5f)));
  uint8_t alphaRow[N];const unsigned opacities[]={22,40,110};
  for(int y=y0;y<=y1;++y){
    std::memset(alphaRow,0,rect.width);
    int firstPixel=rect.x+rect.width,lastPixel=rect.x-1;
    for(int layer=0;layer<3;++layer){
      float left[2],right[2],first=1000,last=-1000,solidLeft=-1000,solidRight=1000;
      for(int sub=0;sub<2;++sub){
        const float scan=y+(sub+.5f)*.5f-.5f;left[sub]=1000;right[sub]=-1000;
        for(int i=0,j=p[layer].count-1;i<p[layer].count;j=i++)if((p[layer].p[i].y>scan)!=(p[layer].p[j].y>scan)){
          const float x=p[layer].p[i].x+(scan-p[layer].p[i].y)*p[layer].slope[i];
          left[sub]=std::min(left[sub],x);right[sub]=std::max(right[sub],x);
        }
        first=std::min(first,left[sub]);last=std::max(last,right[sub]);solidLeft=std::max(solidLeft,left[sub]);solidRight=std::min(solidRight,right[sub]);
      }
      const int begin=std::max(rect.x,static_cast<int>(floorBound(first))),end=std::min(rect.x+rect.width-1,static_cast<int>(ceilBound(last)));
      firstPixel=std::min(firstPixel,begin);lastPixel=std::max(lastPixel,end);
      const int solidBegin=std::max(begin,static_cast<int>(ceilBound(solidLeft+.5f))),solidEnd=std::min(end,static_cast<int>(floorBound(solidRight-.5f)));
      for(int x=begin;x<=end;++x){
        unsigned alpha=opacities[layer];
        if(x<solidBegin||x>solidEnd){
          float coverage=0;for(int sub=0;sub<2;++sub)coverage+=clamp(std::min(x+.5f,right[sub])-std::max(x-.5f,left[sub]),0,1);
          alpha=static_cast<unsigned>(coverage*(alpha*.5f)+.5f);
        }
        auto& value=alphaRow[x-rect.x];value=static_cast<uint8_t>(value+((255-value)*alpha+127)/255);
      }
    }
    uint16_t* row=pixels+(y-rect.y)*rect.width;
    for(int x=firstPixel;x<=lastPixel;++x){const unsigned alpha=alphaRow[x-rect.x];if(alpha){uint16_t color=wire?swap(row[x-rect.x]):row[x-rect.x];color=blend(color,0,alpha);row[x-rect.x]=wire?swap(color):color;}}
  }
}
Polygon expanded(const Polygon& source,float radius){
  Polygon p;p.count=source.count;
  Point normals[20];
  for(int i=0;i<p.count;++i){const auto a=source.p[i],b=source.p[(i+1)%p.count];float dx=b.x-a.x,dy=b.y-a.y,k=1/std::sqrt(dx*dx+dy*dy);normals[i]={dy*k,-dx*k};}
  for(int i=0;i<p.count;++i){
    Point a=normals[(i+p.count-1)%p.count],b=normals[i];float dx=a.x+b.x,dy=a.y+b.y;
    float factor=std::min(radius/std::max(.01f,1+a.x*b.x+a.y*b.y),2*radius/std::max(.01f,std::sqrt(dx*dx+dy*dy)));
    p.p[i]={source.p[i].x+dx*factor,source.p[i].y+dy*factor};
  }
  return p;
}
const Point Long[]={{-5,10},{-7,-135},{0,-171},{7,-135},{5,10}};
const Point Short[]={{-10,10},{-16,-63},{-16,-70},{0,-106},{16,-70},{12,10}};
const Point Stem[]={{-1.1f,12},{-1.1f,-187},{1.1f,-187},{1.1f,12}};
const Point Triangle[]={{-14,-207},{14,-207},{0,-178}};
const Point Tail[]={{-6,5},{6,5},{7,44},{-7,44}};
}
using namespace altimeter_detail;
struct AltimeterRenderer::Data {
  struct Cap {uint16_t color;uint8_t alpha,shadow;};
  struct Geometry {Polygon shape[6],shadow[6][3];};
  uint16_t* face=nullptr;Cap* cap=nullptr;
  Geometry* base=nullptr;Geometry* pose[FrameSlots]={};
  float lastAltitude[FrameSlots]={};bool prepared[FrameSlots]={};
  ~Data(){std::free(face);std::free(cap);std::free(base);for(auto p:pose)std::free(p);}
};
AltimeterRenderer::~AltimeterRenderer(){delete data_;}
bool AltimeterRenderer::begin(){
  if(data_)return true;
  Data* d=new(std::nothrow) Data;if(!d)return false;
  d->face=static_cast<uint16_t*>(memory(N*N*2));d->cap=static_cast<Data::Cap*>(memory(52*52*sizeof(Data::Cap)));
  d->base=static_cast<Data::Geometry*>(memory(sizeof(Data::Geometry)));for(auto& p:d->pose)p=static_cast<Data::Geometry*>(memory(sizeof(Data::Geometry)));
  if(!d->face||!d->cap||!d->base||!d->pose[0]||!d->pose[1]||!d->pose[2]){delete d;return false;}
  new(d->base) Data::Geometry();for(auto p:d->pose)new(p) Data::Geometry();
  Point round[20];for(int i=0;i<20;++i){float t=i*(2*PI/20);round[i]={13*std::cos(t),48+13*std::sin(t)};}
  d->base->shape[0]=polygon(Stem,4,0);d->base->shape[1]=polygon(Triangle,3,0);d->base->shape[2]=polygon(Short,6,0);
  d->base->shape[3]=polygon(Tail,4,0);d->base->shape[4]=polygon(round,20,0);d->base->shape[5]=polygon(Long,5,0);
  for(int part=0;part<6;++part)for(int layer=0;layer<3;++layer)d->base->shadow[part][layer]=expanded(d->base->shape[part],5-2*layer);
  for(int y=0;y<N;++y)for(int x=0;x<N;++x){
    float dx=x-C,dy=y-C,r=std::sqrt(dx*dx+dy*dy);
    uint16_t color=blend(rgb(25,25,24),rgb(50,50,49),static_cast<unsigned>(clamp(124.5f-r,0,1)*255));
    // Reference's striped lower inset is a decorative part of this simulated dial.
    if(dy>0&&r>55&&r<125&&std::fabs(dx)<dy*.55f+1){
      const float mask=clamp(r-55.5f,0,1)*clamp(124.5f-r,0,1)*clamp((dy*.55f-std::fabs(dx))*.876216f+.5f,0,1);
      const float phase=std::fmod(float(x+y),26.f);
      const float stripe=clamp(std::min(phase,13-phase)*.7071068f+.5f,0,1);
      const auto detail=blend(rgb(25,25,24),Cream,static_cast<unsigned>(stripe*255));
      color=blend(color,detail,static_cast<unsigned>(mask*255));
    }
    d->face[y*N+x]=blend(0,color,static_cast<unsigned>(clamp(C+.5f-r,0,1)*255));
  }
  for(int tick=0;tick<50;++tick){
    const float a=tick*(2*PI/50),cs=std::cos(a),sn=std::sin(a);bool major=tick%5==0;
    dialart::line(d->face,{C+209*sn,C-209*cs},{C+(major?183:193)*sn,C-(major?183:193)*cs},major?6:3.6f,Cream);
    if(major){char label[2]={static_cast<char>('0'+tick/5),0};dialart::lettering(d->face,label,C+148*sn,C-148*cs+2,68,Cream);}
  }
  dialart::lettering(d->face,"ALT",C-50,C-44,42,Cream);
  dialart::lettering(d->face,"10",C-53,C-164,16,Cream);
  dialart::lettering(d->face,"METERS",C+52,C-163,14,Cream);
  for(int y=0;y<52;++y)for(int x=0;x<52;++x){
    float dx=x-25.5f,dy=y-25.5f,r=std::sqrt(dx*dx+dy*dy);auto& cap=d->cap[y*52+x];
    cap.shadow=static_cast<uint8_t>(clamp((22-std::sqrt((dx-2)*(dx-2)+(dy-3)*(dy-3)))*.2f,0,1)*165);
    cap.alpha=static_cast<uint8_t>(clamp(18.5f-r,0,1)*255);
    int metal=static_cast<int>(clamp((dx*36+dy*20+848)/1696,0,1)*108+52);
    cap.color=rgb(metal,metal+1,metal-3);
    cap.color=blend(cap.color,rgb(29,31,28),static_cast<unsigned>(clamp(11.3f-r,0,1)*255));
    cap.color=blend(cap.color,rgb(55,57,54),static_cast<unsigned>(clamp(9.7f-r,0,1)*255));
  }
  if(wireOrder_)for(int i=0;i<N*N;++i)d->face[i]=swap(d->face[i]);
  data_=d;return true;
}
void AltimeterRenderer::renderBackgroundRectangle(uint16_t* pixels,AirspeedRect r) const{
  if(!valid(pixels,r))return;
  for(int y=0;y<r.height;++y){
    if(data_)std::memcpy(pixels+y*r.width,data_->face+(y+r.y)*N+r.x,r.width*2);
    else std::memset(pixels+y*r.width,0,r.width*2);
  }
}
bool AltimeterRenderer::prepareFrame(unsigned slot,float altitude) const{
  if(!data_||slot>=FrameSlots)return false;
  if(!std::isfinite(altitude)){data_->prepared[slot]=false;return false;}
  altitude=clamp(altitude,0,altitude_scale::MaxM);
  if(!data_->prepared[slot]||data_->lastAltitude[slot]!=altitude){
    float cs[3],sn[3];for(int hand=0;hand<3;++hand){float a=angle(altitude,hand);cs[hand]=std::cos(a);sn[hand]=std::sin(a);}
    const int hands[]={2,2,1,0,0,0};
    for(int part=0;part<6;++part)for(int layer=-1;layer<3;++layer){
      const auto& base=layer<0?data_->base->shape[part]:data_->base->shadow[part][layer];
      auto& p=layer<0?data_->pose[slot]->shape[part]:data_->pose[slot]->shadow[part][layer];
      p.count=base.count;
      for(int i=0;i<p.count;++i){Point local={base.p[i].x-C,base.p[i].y-C};p.p[i]=transform(local,cs[hands[part]],sn[hands[part]]);if(layer>=0){p.p[i].x+=3;p.p[i].y+=4;}}
      slopes(p);
    }
    data_->lastAltitude[slot]=altitude;data_->prepared[slot]=true;
  }
  return true;
}
void AltimeterRenderer::renderForegroundRectangle(uint16_t* pixels,float altitude,AirspeedRect r) const{
  if(valid(pixels,r)&&prepareFrame(0,altitude))renderForegroundPrepared(pixels,0,r);
}
void AltimeterRenderer::renderForegroundPrepared(uint16_t* pixels,unsigned slot,AirspeedRect r) const{
  if(!valid(pixels,r)||!data_||slot>=FrameSlots||!data_->prepared[slot])return;
  const uint16_t colors[]={Cream,Cream,Cream,rgb(103,104,100),rgb(123,124,120),Cream};
  for(int part=0;part<6;++part){
    shadowGroup(pixels,r,data_->pose[slot]->shadow[part],wireOrder_);
    fill(pixels,r,data_->pose[slot]->shape[part],colors[part],255,wireOrder_);
  }
  for(int y=std::max(r.y,207);y<std::min(r.y+r.height,259);++y)for(int x=std::max(r.x,207);x<std::min(r.x+r.width,259);++x){
    const auto cap=data_->cap[(y-207)*52+x-207];uint16_t& out=pixels[(y-r.y)*r.width+x-r.x];
    uint16_t color=wireOrder_?swap(out):out;color=blend(color,0,cap.shadow);color=blend(color,cap.color,cap.alpha);out=wireOrder_?swap(color):color;
  }
}
void AltimeterRenderer::renderRectangle(uint16_t* pixels,float altitude,AirspeedRect r) const{renderBackgroundRectangle(pixels,r);renderForegroundRectangle(pixels,altitude,r);}
void AltimeterRenderer::render(uint16_t* pixels,float altitude) const{renderRectangle(pixels,altitude,{0,0,N,N});}
AltimeterDamage AltimeterRenderer::damage(float previous,float next){
  AltimeterDamage result;
  if(!std::isfinite(previous)||!std::isfinite(next)){result.regions[0]={0,0,N,N};result.count=1;return result;}
  Polygon bounds[6];int n=0;
  for(float value:{previous,next})for(int hand=0;hand<3;++hand){
    const float width=hand==0?23:hand==1?20:16,top=hand==0?-174:hand==1?-109:-210,bottom=hand==0?63:13;
    const Point corners[]={{-width,top},{width,top},{width,bottom},{-width,bottom}};
    bounds[n++]=polygon(corners,4,angle(value,hand));
  }
  for(int y=0;y<N;y+=32){
    int rows=std::min(32,N-y);float left=1000,right=-1000,lo=y-12,hi=y+rows-1+12;
    for(const auto& p:bounds){
      for(int i=0,j=3;i<4;j=i++){
        const auto a=p.p[i],b=p.p[j];
        if(a.y>=lo&&a.y<=hi){left=std::min(left,a.x-12);right=std::max(right,a.x+12);}
        for(float scan:{lo,hi})if((a.y>scan)!=(b.y>scan)){
          float x=a.x+(scan-a.y)*(b.x-a.x)/(b.y-a.y);left=std::min(left,x-12);right=std::max(right,x+12);
        }
      }
    }
    if(y<259&&y+rows>207){left=std::min(left,206.f);right=std::max(right,260.f);}
    if(right<left)continue;
    const int x=std::max(0,static_cast<int>(floorBound(left)))&~1,end=std::min(N,(static_cast<int>(ceilBound(right))+2)&~1);
    result.regions[result.count++]={x,y,end-x,rows};
  }
  return result;
}
}
