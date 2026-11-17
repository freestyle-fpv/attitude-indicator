#ifndef INSTRUMENT_STYLE_H
#define INSTRUMENT_STYLE_H
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include "hud_font_sampling.h"
#include "raster_math.h"

namespace instrument { namespace style {
// Shared artwork for the production renderer and the older raster engines
// used to verify caching, clipping and wire order.
const float Center=232.5f, Ball=174.f, Pi=3.14159265358979323846f;
const uint16_t White=0xffde;
constexpr float HorizonEdge=3.f;
constexpr float HorizonGuard=HorizonEdge+.5f;
constexpr int32_t HorizonEdgeQ18=int32_t(HorizonEdge*(1<<18));
constexpr int32_t HorizonGuardQ18=int32_t(HorizonGuard*(1<<18));

inline float clamp(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
inline float smooth(float lo,float hi,float v){
  const float t=clamp((v-lo)/(hi-lo),0,1);return t*t*(3-2*t);
}
inline uint16_t rgb(float r,float g,float b){
  return (int(clamp(r,0,255))>>3)<<11 | (int(clamp(g,0,255))>>2)<<5 | (int(clamp(b,0,255))>>3);
}
struct Paint {float sr,sg,sb,gr,gg,gb,visibility;};
inline Paint sample(float dx,float dy){
  const float r2=dx*dx+dy*dy,r=std::sqrt(r2);
  const float radius=r<Ball?Ball:Center;
  const float depth=std::sqrt(std::fmax(0.f,radius*radius-r2));
  float shade=.56f+.44f*depth/radius+.06f*(-dx-dy)/radius;
  if(radius==Center)shade=.88f+.09f*(-dy)/Center;
  float coverage=0,lip=0,shadow=1;
  if(r>149 && r<178){
    // Light falls from above-left. The upper lip casts a broad inward shadow;
    // the lower lip has a small reflected highlight and a tighter contact edge.
    const float top=clamp(.5f-.5f*(dy+.30f*dx)/r,0,1);
    const float distance=std::fmax(0.f,172.f-r)/(4.5f+8.f*top);
    if(r<172.5f)shadow=1-(.32f+.35f*top)*std::exp(-distance*distance);
    shade*=shadow;
    coverage=smooth(170.5f,172.5f,r)*(1-smooth(174.7f,177.f,r));
    lip=(1-top)*.13f*std::exp(-(r-170.5f)*(r-170.5f)/1.7f);
  }
  shade+=lip;
  const float light=r>0?clamp(.5f+.5f*dy/r,0,1):0;
  const float rr=10+8*light,rg=25+7*light,rb=29+5*light,base=1-coverage;
  return {74*shade*base+rr*coverage,179*shade*base+rg*coverage,214*shade*base+rb*coverage,
          160*shade*base+rr*coverage,116*shade*base+rg*coverage,61*shade*base+rb*coverage,
          base*shadow};
}
inline uint8_t background(int x,int y,uint16_t& sky,uint16_t& ground){
  const float dx=x-Center,dy=y-Center,r2=dx*dx+dy*dy;
  Paint p={};
  if(r2>169.5f*169.5f && r2<178.f*178.f){
    // 4x4 area sampling at the curved edge, before RGB565 quantization. No
    // supersampling or extra per-pixel work is needed in the frame loop.
    for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
      const Paint q=sample(dx+(sx-1.5f)*.25f,dy+(sy-1.5f)*.25f);
      p.sr+=q.sr/16;p.sg+=q.sg/16;p.sb+=q.sb/16;
      p.gr+=q.gr/16;p.gg+=q.gg/16;p.gb+=q.gb/16;
      p.visibility+=q.visibility/16;
    }
  }else p=sample(dx,dy);
  const int bayer[16]={0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5};
  const float noise=(bayer[(y&3)*4+(x&3)]-7.5f)*.5f;
  sky=rgb(p.sr+noise,p.sg+noise*.5f,p.sb+noise);
  ground=rgb(p.gr+noise,p.gg+noise*.5f,p.gb+noise);
  // The horizon is composed later. Retain the lip's transmittance so the white
  // line cannot overwrite a nearly opaque antialiased rim pixel.
  return static_cast<uint8_t>(clamp(p.visibility,0,1)*255+.5f);
}

template<class Canvas> void bank(Canvas& c,float roll){
  c.limit=Center;
  const int angles[]={-60,-45,-30,-10,10,30,45,60};
  for(unsigned i=0;i<sizeof(angles)/sizeof(angles[0]);++i){
    float a=angles[i]*Pi/180-roll,sa=std::sin(a),ca=std::cos(a);
    const bool major=std::abs(angles[i])==30||std::abs(angles[i])==60;
    const float inner=181,outer=major?219:207;
    if(std::abs(angles[i])==45){
      float tx=Center+181*sa,ty=Center-181*ca;
      c.triangle(tx,ty,Center+204*sa-7*ca,Center-204*ca-7*sa,Center+204*sa+7*ca,Center-204*ca+7*sa,White);
    }else c.line(Center+inner*sa,Center-inner*ca,Center+outer*sa,Center-outer*ca,major?5.f:4.f,White);
  }
  float cr=std::cos(roll),sr=std::sin(roll);
  c.triangle(Center-177.5f*sr,Center-177.5f*cr,
             Center-8*cr-218.5f*sr,Center+8*sr-218.5f*cr,
             Center+8*cr-218.5f*sr,Center-8*sr-218.5f*cr,White);
}
constexpr float PitchDigitScale=22.f/hudfont::Size;
constexpr float PitchDigitAdvance=hudfont::Advance*PitchDigitScale;

template<class Canvas> void paintPitchDigit(Canvas& c,int digit,float cx,float cy,float cr,float sr,
                                           int firstRow,int endRow,int firstColumn,int endColumn){
  const auto& ink=hudfont::inkBounds[digit];
  constexpr float originX=hudfont::OriginX+hudfont::Advance*.5f;
  const float left=(ink.l-1-originX)*PitchDigitScale,right=(ink.r+1-originX)*PitchDigitScale;
  const float top=(ink.t-1-hudfont::DigitCenterY)*PitchDigitScale,bottom=(ink.b+1-hudfont::DigitCenterY)*PitchDigitScale;
  const float u=(left+right)*.5f,v=(top+bottom)*.5f;
  const float du=(right-left)*.5f,dv=(bottom-top)*.5f;
  const float x=cx+u*cr+v*sr,y=cy-u*sr+v*cr;
  const float wx=du*std::fabs(cr)+dv*std::fabs(sr),wy=du*std::fabs(sr)+dv*std::fabs(cr);
  const int y0=ceilBound(maxBound(float(firstRow),y-wy-.001f));
  const int y1=floorBound(minBound(float(endRow-1),y+wy+.001f));
  if(y0>y1)return;
  const int x0=ceilBound(maxBound(float(firstColumn),x-wx-.001f));
  const int x1=floorBound(minBound(float(endColumn-1),x+wx+.001f));
  const uint8_t* glyph=hudfont::glyphs()[digit];
  constexpr float inverseScale=1.f/PitchDigitScale;
  for(int yy=y0;yy<=y1;++yy)for(int xx=x0;xx<=x1;++xx){
    const float dx=xx-cx,dy=yy-cy;
    const float sx=originX+(dx*cr-dy*sr)*inverseScale;
    const float sy=hudfont::DigitCenterY+(dx*sr+dy*cr)*inverseScale;
    const float alpha=hudfont::sampleGlyph(glyph,sx,sy);
    if(alpha>0)c.pixel(xx,yy,White,alpha*(1.f/255.f));
  }
}

template<class Canvas> void ladder(Canvas& c,float pitch,float cr,float sr){
  for(int latitude=-80;latitude<=80;latitude+=10){
    if(latitude==0)continue;
    float angle=latitude*Pi/180-pitch;
    if(std::cos(angle)<.15f || std::fabs(angle)>40*Pi/180)continue;
    float sine=std::sin(angle);
    const bool major=latitude%20==0;
    const float half=major?61.f:44.f;
    const float width=major?4.4f:3.6f;
    const int segments=major?29:20;
    const float step=2*half/segments;
    float py=-std::sqrt(Ball*Ball-half*half)*sine,px=-half;
    for(int i=1;i<=segments;++i){
      float next=-half+i*step,y=-std::sqrt(Ball*Ball-next*next)*sine;
      c.line(Center+px*cr+py*sr,Center-px*sr+py*cr,Center+next*cr+y*sr,Center-next*sr+y*cr,width,White);px=next;py=y;
    }
    const float labelX=half+19.f,labelY=-std::sqrt(Ball*Ball-labelX*labelX)*sine;
    const int tens=std::abs(latitude)/10;
    // Paint the two labels in the same rotated coordinate system as the rung.
    for(int side:{-1,1}){
      const float first=side*labelX-PitchDigitAdvance*.5f,second=first+PitchDigitAdvance;
      c.pitchDigit(tens,Center+first*cr+labelY*sr,Center-first*sr+labelY*cr,cr,sr);
      c.pitchDigit(0,Center+second*cr+labelY*sr,Center-second*sr+labelY*cr,cr,sr);
    }
  }
}

inline float segmentDistance(float x,float y,float ax,float ay,float bx,float by){
  const float vx=bx-ax,vy=by-ay;
  const float t=clamp(((x-ax)*vx+(y-ay)*vy)/(vx*vx+vy*vy),0,1);
  const float dx=x-ax-t*vx,dy=y-ay-t*vy;
  return std::sqrt(dx*dx+dy*dy);
}
inline float aircraftDistance(float x,float y){
  const float dx=x-Center,dy=y-Center;
  const float wing=std::fmin(segmentDistance(x,y,113,Center,194,Center),
                            segmentDistance(x,y,271,Center,352,Center))-5;
  // The existing U is elliptical; its vertical difference from this distance
  // approximation is below the shadow's blur radius.
  const float arc=dy>=0?std::fabs(std::sqrt(dx*dx+dy*dy*(38.5f/40)*(38.5f/40))-38.5f)-5:1000;
  const float stem=segmentDistance(x,y,Center,244,Center,420)-6;
  const float dot=std::sqrt(dx*dx+dy*dy)-8;
  return std::fmin(std::fmin(wing,arc),std::fmin(stem,dot));
}
inline float aperture(float x,float y){
  const float dx=x-Center,dy=y-Center,r2=dx*dx+dy*dy;
  if(r2<170.f*170.f)return 1;
  if(r2>174.f*174.f)return 0;
  float visible=0;
  for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx){
    const float xx=dx+(sx-1.5f)*.25f,yy=dy+(sy-1.5f)*.25f;
    visible+=(1-smooth(170.5f,172.5f,std::sqrt(xx*xx+yy*yy)))/16;
  }
  return visible;
}
inline float triangleDistance(float x,float y){
  const float ax=Center,ay=61,bx=Center-13,by=95,cx=Center+13,cy=95;
  const float distance=std::fmin(segmentDistance(x,y,ax,ay,bx,by),
                     std::fmin(segmentDistance(x,y,bx,by,cx,cy),segmentDistance(x,y,cx,cy,ax,ay)));
  const bool inside=y<=95 && x>=Center-(y-61)*13/34 && x<=Center+(y-61)*13/34;
  return inside?-distance:distance;
}
template<class Canvas> void fixed(Canvas& c){
  c.limit=Center;
  // One translucent operation per shadow pixel, after the moving white marks.
  // Shadows and antialiased paint are recorded once by the renderer's cache.
  for(int y=58;y<=104;++y)for(int x=215;x<=254;++x){
    const float d=triangleDistance(x-1.5f,y-4.f);
    const float alpha=.58f*(1-smooth(-1.2f,3.2f,d));
    if(alpha>0)c.pixel(x,y,0,alpha);
  }
  for(int y=221;y<=407;++y){
    const int left=y<286?103:221,right=y<286?365:250;
    for(int x=left;x<=right;++x){
      const float d=aircraftDistance(x-1.5f,y-3.8f);
      const float alpha=.56f*(1-smooth(-1.2f,3.f,d))*aperture(x,y);
      if(alpha>0)c.pixel(x,y,0,alpha);
    }
  }
  const uint16_t yellow=0xff42;
  c.triangle(Center,61,Center-13,95,Center+13,95,yellow);
  // Extend the support behind the lower lip. The same subpixel aperture as the
  // rim hides its end, with a short contact shadow on the yellow surface.
  for(int y=238;y<=407;++y)for(int x=225;x<=240;++x){
    const float coverage=clamp(6.5f-segmentDistance(x,y,Center,244,Center,420),0,1);
    const float alpha=coverage*aperture(x,y);
    const float light=1-.36f*smooth(395.f,405.f,float(y));
    if(alpha>0)c.pixel(x,y,rgb(235*light,190*light,56*light),alpha);
  }
  c.line(113,Center,194,Center,10,yellow);
  c.line(271,Center,352,Center,10,yellow);
  float px=194,py=Center;
  for(int i=1;i<=36;++i){
    const float a=Pi-Pi*i/36,x=Center+38.5f*std::cos(a),y=Center+40*std::sin(a);
    c.line(px,py,x,y,10,yellow);px=x;py=y;
  }
  c.line(Center,Center,Center,Center,16,yellow);
}
} }
#endif
