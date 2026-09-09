#ifndef INSTRUMENT_STYLE_H
#define INSTRUMENT_STYLE_H
#include <cmath>
#include <cstdint>

namespace instrument { namespace style {
// Paint and fixed geometry are evaluated only during renderer initialization.
// Keeping them separate lets the frozen raster engines exercise the new art.
const float Center=232.5f, Ball=174.f, Pi=3.14159265358979323846f;
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
