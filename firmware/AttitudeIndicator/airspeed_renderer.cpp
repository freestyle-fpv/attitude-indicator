#include "airspeed_renderer.h"
#include "dial_art.h"
#include "circle_band.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
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
namespace {
constexpr int N=466,SW=48,SH=254,SX=-24,SY=-182;
constexpr float C=232.5f,PI=3.14159265358979323846f;
using dialart::Point;
using dialart::inside;
using dialart::line;
using dialart::lettering;
const Point hand[]={{-5,10},{-7,-135},{0,-171},{7,-135},{5,10}};
const Point tail[]={{-6,5},{6,5},{7,44},{-7,44}};
inline __attribute__((always_inline)) float bounded(float v,float low,float high){return v<low?low:(v>high?high:v);}
inline __attribute__((always_inline)) uint16_t swap(uint16_t c){return static_cast<uint16_t>((c<<8)|(c>>8));}
inline __attribute__((always_inline)) uint16_t rgb(int r,int g,int b){return static_cast<uint16_t>((r>>3)<<11|(g>>2)<<5|(b>>3));}
inline __attribute__((always_inline)) uint16_t mix8(uint16_t a,uint16_t b,uint8_t alpha){
  if(!alpha)return a;
  if(alpha==255)return b;
  const unsigned t=alpha+(alpha>=128),u=256-t;
  return static_cast<uint16_t>(((((a>>11)*u+(b>>11)*t+128)>>8)<<11)|
    (((((a>>5)&63)*u+((b>>5)&63)*t+128)>>8)<<5)|(((a&31)*u+(b&31)*t+128)>>8));
}
uint16_t mix(uint16_t a,uint16_t b,float opacity){
  const int t=static_cast<int>(bounded(opacity,0,1)*256+.5f),u=256-t;
  return static_cast<uint16_t>(((((a>>11)*u+(b>>11)*t+128)>>8)<<11)|
    (((((a>>5)&63)*u+((b>>5)&63)*t+128)>>8)<<5)|(((a&31)*u+(b&31)*t+128)>>8));
}
void* memory(size_t size,bool internal=false){
#if defined(ARDUINO_ARCH_ESP32)
  if(internal&&heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)>size+32*1024){
    void* result=heap_caps_malloc(size,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(result)return result;
  }
  return heap_caps_malloc(size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  (void)internal;
  return std::malloc(size);
#endif
}
bool valid(uint16_t* p,int row,int rows){return p&&row>=0&&row<N&&rows>0&&rows<=N-row;}
float radians(float speed){return (-135+bounded(speed,0,200)*1.35f)*PI/180;}
Point polar(float radius,float a){return {C+radius*std::sin(a),C-radius*std::cos(a)};}
// Only opacity needs bilinear filtering. The cream/metal materials are analytic;
// two small masks fit in internal SRAM instead of evicting the PSRAM face cache.
inline __attribute__((always_inline)) uint8_t sampleMask(const uint8_t* image,int32_t x,int32_t y){
  // Q16 coordinates retain the same sampling origin for full frames and crops.
  x-=SX*65536;y-=SY*65536;
  if(static_cast<uint32_t>(x)>=static_cast<uint32_t>((SW-1)*65536)||
     static_cast<uint32_t>(y)>=static_cast<uint32_t>((SH-1)*65536))return 0;
  const int ix=x>>16,iy=y>>16,fx=(x>>8)&255,fy=(y>>8)&255;
  const uint8_t* p=image+iy*SW+ix;
  const int top=p[0]*(256-fx)+p[1]*fx,bottom=p[SW]*(256-fx)+p[SW+1]*fx;
  return static_cast<uint8_t>((top*(256-fy)+bottom*fy+32768)>>16);
}
}
struct AirspeedRenderer::Data {
  uint16_t* face=nullptr;
  uint8_t* handImage=nullptr;
  uint8_t* shadow=nullptr;
  struct Cap { uint16_t color;uint8_t alpha,shadow; };
  Cap* cap=nullptr;
  ~Data(){std::free(face);std::free(handImage);std::free(shadow);std::free(cap);}
};
AirspeedRenderer::AirspeedRenderer(bool wireOrder):wireOrder_(wireOrder){}
AirspeedRenderer::~AirspeedRenderer(){delete data_;}
bool AirspeedRenderer::begin(){
  if(data_)return true;
  Data* d=new(std::nothrow) Data;if(!d)return false;
  d->face=static_cast<uint16_t*>(memory(N*N*2));
  d->handImage=static_cast<uint8_t*>(memory(SW*SH,true));
  d->shadow=static_cast<uint8_t*>(memory(SW*SH,true));
  d->cap=static_cast<Data::Cap*>(memory(52*52*sizeof(Data::Cap)));
  float* temporary=static_cast<float*>(memory(SW*SH*sizeof(float)));
  if(!d->face||!d->handImage||!d->shadow||!d->cap||!temporary){std::free(temporary);delete d;return false;}
  const uint16_t black=rgb(25,25,24),disc=rgb(50,50,49),cream=rgb(255,251,208);
  for(int y=0;y<N;++y)for(int x=0;x<N;++x){
    float dx=x-C,dy=y-C,r=std::sqrt(dx*dx+dy*dy),angle=std::atan2(dx,-dy)*180/PI;
    uint16_t color=mix(black,disc,bounded(124.5f-r,0,1));
    if(angle>=-135&&angle<=135){
      uint16_t band=angle<54?rgb(118,173,112):(angle<108?rgb(229,203,93):rgb(223,91,78));
      color=mix(color,band,bounded(r-(C-12)+.5f,0,1));
    }
    d->face[y*N+x]=mix(0,color,bounded(C+.5f-r,0,1));
  }
  for(int speed=0;speed<=200;speed+=5){
    bool major=speed%20==0,middle=speed%10==0;float a=radians(speed);
    line(d->face,polar(210,a),polar(major?186:middle?196:199,a),major?6:3.6f,cream);
    if(major){char text[4];std::snprintf(text,sizeof(text),"%d",speed);Point p=polar(148,a);lettering(d->face,text,p.x,p.y+2,34,cream);}
  }
  lettering(d->face,"AIR SPEED",C,176.5f,21,cream);lettering(d->face,"km/h",C,316.5f,20,cream);
  // Precompute the unrotated hand from geometric shapes at 4x4 coverage.
  for(int y=0;y<SH;++y)for(int x=0;x<SW;++x){
    int alpha=0;
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
      float px=SX+x+(sx+.5f)/4-.5f,py=SY+y+(sy+.5f)/4-.5f;
      if(inside(px,py,hand,5)||inside(px,py,tail,4)||px*px+(py-48)*(py-48)<=169)++alpha;
    }
    d->handImage[y*SW+x]=static_cast<uint8_t>(alpha*255/16);
  }
  // A separable Gaussian is built only once; moving frames just sample it.
  float kernel[19],sum=0;
  for(int k=-9;k<=9;++k){kernel[k+9]=std::exp(-k*k/18.f);sum+=kernel[k+9];}
  for(float& k:kernel)k/=sum;
  for(int y=0;y<SH;++y)for(int x=0;x<SW;++x){float v=0;for(int k=-9;k<=9;++k)if(x+k>=0&&x+k<SW)v+=d->handImage[y*SW+x+k]*kernel[k+9];temporary[y*SW+x]=v;}
  for(int y=0;y<SH;++y)for(int x=0;x<SW;++x){float v=0;for(int k=-9;k<=9;++k)if(y+k>=0&&y+k<SH)v+=temporary[(y+k)*SW+x]*kernel[k+9];d->shadow[y*SW+x]=static_cast<uint8_t>(v*.8f+.5f);}
  // Fixed center cap: no sqrt, gradients or floating point blending per frame.
  for(int y=0;y<52;++y)for(int x=0;x<52;++x){
    float dx=x-25.5f,dy=y-25.5f,distance=std::sqrt(dx*dx+dy*dy);
    auto& cap=d->cap[y*52+x];
    cap.shadow=static_cast<uint8_t>(bounded((22-std::sqrt((dx-2)*(dx-2)+(dy-3)*(dy-3)))*.2f,0,1)*.65f*255+.5f);
    cap.alpha=static_cast<uint8_t>(bounded(18.5f-distance,0,1)*255+.5f);
    float t=bounded((dx*36+dy*20+848)*(1.f/1696),0,1);
    uint16_t metal=t<.5f?mix(rgb(52,53,52),rgb(98,100,95),t*2):mix(rgb(98,100,95),rgb(160,162,155),(t-.5f)*2);
    metal=mix(metal,rgb(29,31,28),bounded(11.3f-distance,0,1));
    cap.color=mix(metal,rgb(55,57,54),bounded(9.7f-distance,0,1));
  }
  std::free(temporary);
  if(wireOrder_)for(int i=0;i<N*N;++i)d->face[i]=swap(d->face[i]);
  data_=d;return true;
}
void AirspeedRenderer::renderBackgroundBand(uint16_t* pixels,int row,int rows,bool compact) const{
  if(!valid(pixels,row,rows))return;
  CircleBand band=compact?circleBandBounds(row,rows):CircleBand{0,N};
  for(int y=row;y<row+rows;++y){
    uint16_t* out=pixels+(y-row)*band.width;
    if(data_)std::memcpy(out,data_->face+y*N+band.x,band.width*2);
    else std::memset(out,0,band.width*2);
  }
}
void AirspeedRenderer::renderForegroundBand(uint16_t* pixels,float speed,int row,int rows,bool compact) const{
  if(!valid(pixels,row,rows))return;
  CircleBand band=compact?circleBandBounds(row,rows):CircleBand{0,N};
  foreground(pixels,speed,{band.x,row,band.width,rows});
}
AirspeedRect AirspeedRenderer::damage(float previous,float next){
  if(!std::isfinite(previous)||!std::isfinite(next))return {0,0,N,N};
  float left=C-26,right=C+26,top=C-26,bottom=C+26;
  for(float speed:{previous,next}){
    const float a=radians(speed),cs=std::cos(a),sn=std::sin(a);
    for(float x:{float(SX-6),float(SX+SW+6)})for(float y:{float(SY-6),float(SY+SH+6)}){
      const float px=C+x*cs-y*sn,py=C+x*sn+y*cs;
      left=std::min(left,px);right=std::max(right,px);top=std::min(top,py);bottom=std::max(bottom,py);
    }
  }
  int x=std::max(0,static_cast<int>(std::floor(left)))&~1;
  int y=std::max(0,static_cast<int>(std::floor(top)))&~1;
  int endX=std::min(N,(static_cast<int>(std::ceil(right))+2)&~1),endY=std::min(N,(static_cast<int>(std::ceil(bottom))+2)&~1);
  return {x,y,endX-x,endY-y};
}
void AirspeedRenderer::renderRectangle(uint16_t* pixels,float speed,AirspeedRect r) const{
  renderBackgroundRectangle(pixels,r);renderForegroundRectangle(pixels,speed,r);
}
void AirspeedRenderer::renderForegroundRectangle(uint16_t* pixels,float speed,AirspeedRect r) const{
  if(!pixels||r.x<0||r.y<0||r.x>=N||r.y>=N||r.width<=0||r.height<=0||r.width>N-r.x||r.height>N-r.y)return;
  foreground(pixels,speed,r);
}
void AirspeedRenderer::renderBackgroundRectangle(uint16_t* pixels,AirspeedRect r) const{
  if(!pixels||r.x<0||r.y<0||r.x>=N||r.y>=N||r.width<=0||r.height<=0||r.width>N-r.x||r.height>N-r.y)return;
  for(int y=0;y<r.height;++y){
    if(data_)std::memcpy(pixels+y*r.width,data_->face+(r.y+y)*N+r.x,r.width*2);
    else std::memset(pixels+y*r.width,0,r.width*2);
  }
}
void AirspeedRenderer::foreground(uint16_t* pixels,float speed,AirspeedRect rect) const{
  if(!data_||!std::isfinite(speed))return;
  float a=radians(speed),cs=std::cos(a),sn=std::sin(a);
  // Intersect each row with the rotated, padded sprite rectangle. This avoids
  // scanning the large empty corners of a diagonal needle's bounding box.
  Point corners[4];const Point local[4]={{SX-6,SY-6},{SX+SW+6,SY-6},{SX+SW+6,SY+SH+6},{SX-6,SY+SH+6}};
  for(int i=0;i<4;++i)corners[i]={C+local[i].x*cs-local[i].y*sn,C+local[i].x*sn+local[i].y*cs};
  const int32_t stepX=std::lround(cs*65536),stepY=std::lround(-sn*65536);
  const int32_t shadowX=std::lround((3*cs+4*sn)*65536),shadowY=std::lround((-3*sn+4*cs)*65536);
  uint16_t metals[49];
  for(int i=0;i<49;++i){float t=i*(1.f/48);int m=static_cast<int>(t<.55f?36+t*(75.f/.55f):111+(t-.55f)*(41.f/.45f));metals[i]=rgb(m,m,m-2);}
  for(int y=rect.y;y<rect.y+rect.height;++y){
    float first=1000,last=-1000;
    for(int i=0,j=3;i<4;j=i++){
      if((corners[i].y>y)!=(corners[j].y>y)){
        float x=corners[i].x+(y-corners[i].y)*(corners[j].x-corners[i].x)/(corners[j].y-corners[i].y);
        first=std::min(first,x);last=std::max(last,x);
      }
    }
    if(std::abs(y-C)<26){first=std::min(first,C-26);last=std::max(last,C+26);}
    int left=std::max(rect.x,static_cast<int>(std::floor(first))),right=std::min(rect.x+rect.width-1,static_cast<int>(std::ceil(last)));
    const float dy=y-C;
    int32_t localX=std::lround((-C*cs+dy*sn)*65536)+left*stepX;
    int32_t localY=std::lround((C*sn+dy*cs)*65536)+left*stepY;
    for(int x=left;x<=right;++x,localX+=stepX,localY+=stepY){
      uint16_t& pixel=pixels[(y-rect.y)*rect.width+x-rect.x];
      uint16_t color=wireOrder_?swap(pixel):pixel;
      const Data::Cap* cap=(x>=207&&x<259&&y>=207&&y<259)?data_->cap+(y-207)*52+x-207:nullptr;
      if(cap&&cap->alpha==255){pixel=wireOrder_?swap(cap->color):cap->color;continue;}
      const uint8_t alpha=sampleMask(data_->shadow,localX-shadowX,localY-shadowY);
      if(alpha)color=mix8(color,0,alpha);
      const uint8_t handAlpha=sampleMask(data_->handImage,localX,localY);
      if(handAlpha){
        uint16_t material=rgb(255,251,208);
        // The join between materials is hidden underneath the fixed center cap.
        if(localY>0){
          const int index=std::max(0,std::min(48,static_cast<int>((localX+12*65536+16384)/32768)));
          material=metals[index];
        }
        color=mix8(color,material,handAlpha);
      }
      if(cap){color=mix8(color,0,cap->shadow);color=mix8(color,cap->color,cap->alpha);}
      pixel=wireOrder_?swap(color):color;
    }
  }
}
void AirspeedRenderer::render(uint16_t* pixels,float speed){renderBackgroundBand(pixels,0,N);renderForegroundBand(pixels,speed,0,N);}
}
