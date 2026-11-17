#include "dial_art.h"
#include "airspeed_font.h"
#include <algorithm>
#include <cmath>
namespace instrument { namespace dialart {
constexpr int N=466;
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
bool inside(float x,float y,const Point* p,int count){
  bool in=false;
  for(int i=0,j=count-1;i<count;j=i++)
    if((p[i].y>y)!=(p[j].y>y)&&x<(p[j].x-p[i].x)*(y-p[i].y)/(p[j].y-p[i].y)+p[i].x)in=!in;
  return in;
}
void plot(uint16_t* face,int x,int y,uint16_t color,float alpha){
  if(x<0||x>=N||y<0||y>=N)return;
  face[y*N+x]=mix(face[y*N+x],color,alpha);
}
void line(uint16_t* face,Point a,Point b,float width,uint16_t color){
  float vx=b.x-a.x,vy=b.y-a.y,len=vx*vx+vy*vy,half=width/2;
  for(int y=static_cast<int>(std::floor(std::min(a.y,b.y)-half-1));y<=std::ceil(std::max(a.y,b.y)+half+1);++y)
    for(int x=static_cast<int>(std::floor(std::min(a.x,b.x)-half-1));x<=std::ceil(std::max(a.x,b.x)+half+1);++x){
      float t=bounded(((x-a.x)*vx+(y-a.y)*vy)/len,0,1),dx=x-a.x-t*vx,dy=y-a.y-t*vy;
      plot(face,x,y,color,bounded(half+.5f-std::sqrt(dx*dx+dy*dy),0,1));
    }
}
const airfont::Glyph* glyph(char c){
  for(const auto& g:airfont::glyphs)if(g.code==c)return &g;
  return nullptr;
}
void lettering(uint16_t* face,const char* text,float x,float y,float size,uint16_t color){
  const float scale=size/1000;
  float advance=0;
  for(const char* p=text;*p;++p)if(auto g=glyph(*p))advance+=g->advance*scale;
  x-=advance/2;
  const float baseline=y+size*.36f;
  for(const char* p=text;*p;++p){
    auto g=glyph(*p);if(!g)continue;
    // Four subpixel scanlines and exact horizontal coverage, including counters.
    for(int row=static_cast<int>(std::floor(y-size));row<=std::ceil(y+size);++row){
      float coverage[64]={};
      const int origin=static_cast<int>(std::floor(x))-2;
      for(int sample=0;sample<4;++sample){
        const float scan=(baseline-(row+(sample+.5f)/4-.5f))/scale;
        float crossings[128];int count=0;
        for(unsigned e=g->first;e<g->first+g->count;++e){
          const auto& edge=airfont::edges[e];
          if((edge.y1>scan)!=(edge.y2>scan)&&count<128)
            crossings[count++]=x+scale*(edge.x1+(scan-edge.y1)*(edge.x2-edge.x1)/(edge.y2-edge.y1));
        }
        std::sort(crossings,crossings+count);
        for(int e=0;e+1<count;e+=2)for(int col=0;col<64;++col)
          coverage[col]+=bounded(std::min(origin+col+.5f,crossings[e+1])-std::max(origin+col-.5f,crossings[e]),0,1)*.25f;
      }
      for(int col=0;col<64;++col)if(coverage[col]>0)plot(face,origin+col,row,color,coverage[col]);
    }
    x+=g->advance*scale;
  }
}
} }
