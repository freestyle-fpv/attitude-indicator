#include "hud_renderer.h"
#include "hud_font.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#ifdef ARDUINO
#include <esp_heap_caps.h>
#endif
namespace instrument {
namespace huddetail {
constexpr float Pi=3.14159265358979323846f;
constexpr int Capacity=384;
float bound(float x,float a,float b){return std::max(a,std::min(b,x));}
float safe(float x,float fallback=0){return std::isfinite(x)?x:fallback;}
float wrap(float x){x=std::fmod(safe(x),360.f);return x<0?x+360:x;}
float emphasis(float distance,float step){float t=bound(1-std::fabs(distance)/step,0,1);return t*t*(3-2*t);}
struct Clip {float l=0,t=0,r=466,b=466;};
Clip area(float l,float t,float r,float b){Clip c;c.l=l;c.t=t;c.r=r;c.b=b;return c;}
struct Command {
  enum Kind {Line,Ring,Text,Window} kind=Line;
  float x=0,y=0,x2=0,y2=0,size=1.15f,weight=0,cs=1,sn=0;
  Clip clip;
  uint8_t green=198;
  bool fade=false,shade=false;
  char str[16]={};
};
struct List {Command items[Capacity];int count=0;bool valid=false;};
constexpr float CounterCycle=.24f;
struct Wheel {int from=0,to=0;float elapsed=CounterCycle;bool started=false;};
struct Builder {
  explicit Builder(List& value):list(value){}
  List& list;
  Clip clip;
  bool fade=false,shade=false;
  Command& add(Command::Kind kind){
    // The fixed limit bounds all work even after future scale changes.
    Command& c=list.items[std::min(list.count,Capacity-1)];
    if(list.count<Capacity)++list.count;
    c=Command();c.kind=kind;c.clip=clip;c.fade=fade;c.shade=shade;return c;
  }
  void line(float x,float y,float xx,float yy,float width=1.5f,int green=198){auto& c=add(Command::Line);c.x=x;c.y=y;c.x2=xx;c.y2=yy;c.size=width;c.green=green;}
  void ring(float x,float y,float radius){auto& c=add(Command::Ring);c.x=x;c.y=y;c.x2=radius;c.size=1.6f;c.green=255;}
  void text(const char* value,float x,float y,float size,int green=198,float weight=0,float angle=0){auto& c=add(Command::Text);c.x=x;c.y=y;c.size=size;c.green=green;c.weight=weight;c.cs=std::cos(angle);c.sn=std::sin(angle);std::snprintf(c.str,sizeof(c.str),"%s",value);}
  void number(int value,float x,float y,float size=11,int green=198,float weight=0,float angle=0,bool sign=false){char s[16];std::snprintf(s,sizeof(s),sign&&value>0?"+%d":"%d",value);text(s,x,y,size,green,weight,angle);}
  void tick(float x,float y,float dx,float dy,bool major,bool bright=false,float amount=0){float len=major?17:9;line(x,y,x+dx*len,y+dy*len,1.5f+1.7f*amount,bright?255:int((major?198:140)+(255-(major?198:140))*amount));}
  void triangle(float x,float y,float dx,float dy){float bx=x-dx*12,by=y-dy*12;line(x,y,bx-dy*6,by+dx*6,1.6f,255);line(bx-dy*6,by+dx*6,bx+dy*6,by-dx*6,1.6f,255);line(bx+dy*6,by-dx*6,x,y,1.6f,255);}
};
// Matches the HTML footprint, including the largest bold endpoint label.
constexpr float Half=104.f,LabelGap=23.f;
float radius(){return (Half-3*hudfont::Advance*22/hudfont::Size/2)/std::sin(Pi/3)-LabelGap;}
char cardinal(float d){const char names[]="NESW";return names[int(std::round(wrap(d)/90))%4];}
void counter(Builder& b,Wheel& c,float x,float y,int digits){
  char old[8],next[8];std::snprintf(old,sizeof(old),"%*d",digits,c.from);std::snprintf(next,sizeof(next),"%*d",digits,c.to);
  float t=bound(c.elapsed/CounterCycle,0,1),p=t*t*(3-2*t);
  int direction=c.to>=c.from?1:-1;
  b.clip=area(x-33,y-16,x+33,y+16);b.shade=true;
  for(int i=0;i<digits;++i){float dx=x+(i-(digits-1)*.5f)*15.6f;char s[2]={next[i],0};
    if(old[i]==next[i]||t>=1)b.text(s,dx,y,26,255,.4f);
    else {s[0]=old[i];b.text(s,dx,y-direction*p*34,26,255,.4f);s[0]=next[i];b.text(s,dx,y+direction*(1-p)*34,26,255,.4f);}
  }
  b.clip=Clip();b.shade=false;
}
void tape(Builder& b,Wheel& wheel,bool left,float value,float pitch){
  float x=left?85:381,sign=left?-1:1,minor=5,max=left?200:3000;
  float scale=pitch/minor;
  b.line(x+sign*34,122,x,122);b.line(x,122,x,346);b.line(x,346,x+sign*34,346);
  const int lo=std::max(0,int(std::ceil((value-(346-234)/scale)/minor))*int(minor));
  for(int v=lo;v<=std::min(max,value+(234-122)/scale);v+=int(minor)){
    float y=234-(v-value)*scale;bool major=v%20==0;
    if(y<126||y>342)continue;
    b.tick(x,y,sign,0,major);
    if(major&&std::fabs(y-234)>33&&y>137&&y<331){
      char s[12];std::snprintf(s,sizeof(s),"%d",v);float half=std::strlen(s)*hudfont::Advance*18/hudfont::Size/2;
      const float center=x+sign*(22+half),farX=std::fabs(center-233)+half+2,farY=std::fabs(y-233)+13;
      // Keep the complete larger label within the circular screen.
      if(farX*farX+farY*farY<231*231)b.text(s,center,y,18,255,.35f);
    }
  }
  const float inner=x+sign*9,outer=inner+sign*72;
  auto& c=b.add(Command::Window);c.x=x;c.y=234;c.x2=outer;c.y2=sign;
  const float px[]={x,inner,outer-sign*2,outer,outer,outer-sign*2,inner,x};
  const float py[]={234,214,214,216,252,254,254,234};
  for(int i=0;i<7;++i)b.line(px[i],py[i],px[i+1],py[i+1],1.6f,255);
  counter(b,wheel,(inner+outer)/2,234,left?3:4);
  b.text(left?"KM/H":"M",x+sign*17,104,14,255,.25f);
}
void heading(Builder& b,float raw,float pitch){
  float spacing=pitch/5,labelHalf=Half-24,extent=Half/spacing;
  for(int v=int(std::floor((raw-extent)/5))*5;v<=raw+extent;v+=5){
    float x=233+(v-raw)*spacing;if(x<233-Half+1||x>233+Half-1)continue;
    int degrees=int(wrap(float(v)));bool card=degrees%90==0,major=degrees%20==0||card;b.tick(x,72,0,-1,major,card,emphasis(v-raw,5));
    if(major&&(card||(degrees%90!=10&&degrees%90!=80))){char label[8];if(card){label[0]=cardinal(degrees);label[1]=0;}else std::snprintf(label,sizeof(label),"%d",degrees);
      float amount=emphasis(v-raw,20),base=20,size=base+(22-base)*amount;
      float half=std::strlen(label)*hudfont::Advance*size/hudfont::Size/2;
      if(x-half>=233-labelHalf&&x+half<=233+labelHalf)b.text(label,x,44,size,255,.35f+.65f*amount);
    }
  }
  char s[2]={cardinal(std::floor((raw-labelHalf/spacing)/90)*90),0};
  b.text(s,233-Half+hudfont::Advance*20/hudfont::Size/2,44,20,255,.35f);
  s[0]=cardinal(std::ceil((raw+labelHalf/spacing)/90)*90);b.text(s,233+Half-hudfont::Advance*20/hudfont::Size/2,44,20,255,.35f);
  b.triangle(233,77,0,-1);
}
// Tight bitmap ink bounds, including bilinear sampling support. Precomputed;
// blank atlas margins must not hide otherwise fully visible pitch numbers.
bool textFits(const char* s,float x,float y,float size,float cs,float sn,const Clip& clip){
  const float scale=size/hudfont::Size,adv=hudfont::Advance*scale,total=std::strlen(s)*adv;
  for(unsigned i=0;s[i];++i){int g=0;while(g<hudfont::CharCount&&hudfont::chars[g]!=s[i])++g;
    if(g==hudfont::CharCount||hudfont::inkBounds[g].r<0)continue;const auto& q=hudfont::inkBounds[g];
    const float left=-total/2+i*adv-hudfont::OriginX*scale,top=-hudfont::DigitCenterY*scale;
    for(float u:{left+(q.l-1)*scale,left+(q.r+1)*scale})for(float v:{top+(q.t-1)*scale,top+(q.b+1)*scale}){
      const float xx=x+cs*u-sn*v,yy=y+sn*u+cs*v;
      if(xx<clip.l+1||xx>clip.r-1||yy<clip.t+1||yy>clip.b-1)return false;
    }
  }
  return true;
}
void ladder(Builder& b,float pitch,float roll){
  const float angle=-roll*Pi/180,cs=std::cos(angle),sn=std::sin(angle);
  b.clip=area(102,114,364,350);b.fade=true;
  auto line=[&](float x,float y,float xx,float yy,int green){b.line(233+cs*x-sn*y,234+sn*x+cs*y,233+cs*xx-sn*yy,234+sn*xx+cs*yy,1.5f,green);};
  for(int p=-90;p<=90;p+=5){float y=(pitch-p)*12;if(std::fabs(y)>260)continue;
    const bool major=p%10==0;const float half=major?80:48;int green=p==0?255:198;
    if(p>=0)line(-half,y,half,y,green);
    else for(float x=-4-13*std::ceil(half/13);x<half;x+=13){const float a=std::max(-half,x),z=std::min(half,x+8);if(z>a)line(a,y,z,y,green);}
    const float hook=p<0?-6:6;
    line(-half,y,-half,y+hook,green);line(half,y,half,y+hook,green);
    for(int side:{-1,1}){
      char s[8];std::snprintf(s,sizeof(s),"%d",p);float width=std::strlen(s)*hudfont::Advance*20/hudfont::Size;
      float x=side*(half+4+width/2),sx=233+cs*x-sn*y,sy=234+sn*x+cs*y;
      if(textFits(s,sx,sy,20,cs,sn,b.clip)){b.fade=false;b.text(s,sx,sy,20,255,.35f,angle);b.fade=true;}
    }
  }
  b.clip=Clip();b.fade=false;
}
void bank(Builder& b,float roll,bool valid,float r){
  float cy=420-r;
  for(int v=-60;v<=60;v+=10){float a=v*Pi/180,s=std::sin(a),c=std::cos(a);bool major=v%30==0;
    b.tick(233+s*r,cy+c*r,-s,-c,major,false,valid?emphasis(v-roll,10):0);
    if(major){float amt=valid?emphasis(v-roll,30):0;b.number(v,233+s*(r+LabelGap),cy+c*(r+LabelGap),20+2*amt,255,.35f+.65f*amt,0,true);}
  }
  if(valid){float a=roll*Pi/180,s=std::sin(a),c=std::cos(a);b.triangle(233+s*(r-25),cy+c*(r-25),s,c);}
}
int glyphIndex(char c){for(unsigned i=0;i<sizeof(hudfont::chars)-1;++i)if(hudfont::chars[i]==c)return int(i);return -1;}
float sample(const uint8_t* glyph,float x,float y){
  int ix=int(std::floor(x)),iy=int(std::floor(y));if(ix<0||iy<0||ix+1>=hudfont::Width||iy+1>=hudfont::Height)return 0;
  float fx=x-ix,fy=y-iy;const auto* p=glyph+iy*hudfont::Width+ix;
  return (p[0]*(1-fx)+p[1]*fx)*(1-fy)+(p[hudfont::Width]*(1-fx)+p[hudfont::Width+1]*fx)*fy;
}
struct Raster {
  uint16_t* pixels;AirspeedRect rect;bool wire;
  void plot(int x,int y,int green,float coverage,const Command& cmd){
    if(x<rect.x||x>=rect.x+rect.width||y<rect.y||y>=rect.y+rect.height||x<cmd.clip.l||x>=cmd.clip.r||y<cmd.clip.t||y>=cmd.clip.b)return;
    if((x-233)*(x-233)+(y-233)*(y-233)>233*233)return;
    if(cmd.fade)coverage*=bound(std::min(std::min(x-cmd.clip.l,cmd.clip.r-x),std::min(y-cmd.clip.t,cmd.clip.b-y))*(1.f/14.f),0,1);
    if(cmd.shade)coverage*=bound(std::min(y-cmd.clip.t,cmd.clip.b-y)*(1.f/5.3f),0,1);
    const int alpha=int(bound(coverage,0,1)*255+.5f);if(!alpha)return;
    auto& out=pixels[(y-rect.y)*rect.width+x-rect.x];uint16_t old=wire?uint16_t(out<<8|out>>8):out;
    int value=(((old>>5)&63)*(255-alpha)+(green>>2)*alpha+127)/255;
    uint16_t color=uint16_t(value<<5);out=wire?uint16_t(color<<8|color>>8):color;
  }
  void shape(const Command& c){
    float l,t,r,b;if(c.kind==Command::Ring){l=c.x-c.x2-2;r=c.x+c.x2+2;t=c.y-c.x2-2;b=c.y+c.x2+2;}
    else if(c.kind==Command::Window){l=std::min(c.x,c.x2);r=std::max(c.x,c.x2);t=c.y-20;b=c.y+20;}
    else {const float pad=std::max(2.f,c.size*.5f+.5f);l=std::min(c.x,c.x2)-pad;r=std::max(c.x,c.x2)+pad;t=std::min(c.y,c.y2)-pad;b=std::max(c.y,c.y2)+pad;}
    int x0=std::max(rect.x,int(std::floor(std::max(l,c.clip.l)))),x1=std::min(rect.x+rect.width-1,int(std::ceil(std::min(r,c.clip.r))));
    int y0=std::max(rect.y,int(std::floor(std::max(t,c.clip.t)))),y1=std::min(rect.y+rect.height-1,int(std::ceil(std::min(b,c.clip.b))));
    float vx=c.x2-c.x,vy=c.y2-c.y,len=vx*vx+vy*vy;
    const float inverseLength=len>0?1.f/len:0;
    // A long tilted line occupies a narrow strip, not its full bounding box.
    // Intersect each row with that strip, keeping an extra pixel of margin
    // for round caps and floating-point rounding. Coverage itself is unchanged.
    const bool narrowRows=c.kind==Command::Line&&std::fabs(vy)>.01f;
    const float slope=narrowRows?vx/vy:0;
    const float reach=(c.size*.5f+.5f)*std::sqrt(1+slope*slope)+1;
    for(int y=y0;y<=y1;++y){
      int rowLeft=x0,rowRight=x1;
      if(narrowRows){const float center=c.x+(y-c.y)*slope;
        rowLeft=std::max(x0,int(std::floor(center-reach)));rowRight=std::min(x1,int(std::ceil(center+reach)));}
      for(int x=rowLeft;x<=rowRight;++x){float a=0;
      if(c.kind==Command::Ring)a=c.size*.5f+.5f-std::fabs(std::hypot(x-c.x,y-c.y)-c.x2);
      else if(c.kind==Command::Window){float start=c.x+c.y2*9*std::fabs(y-c.y)/20;bool inside=c.y2<0?x<=start:x>=start;if(inside)plot(x,y,0,1,c);continue;}
      else {float q=len>0?bound(((x-c.x)*vx+(y-c.y)*vy)*inverseLength,0,1):0;float dx=x-c.x-q*vx,dy=y-c.y-q*vy;a=c.size*.5f+.5f-std::sqrt(dx*dx+dy*dy);}
      if(a>0)plot(x,y,c.green,a,c);
    }}
  }
  void text(const Command& c){
    const float scale=c.size/hudfont::Size,inverseScale=1.f/scale,adv=hudfont::Advance*scale,total=std::strlen(c.str)*adv;
    for(unsigned i=0;c.str[i];++i){int index=glyphIndex(c.str[i]);if(index<0||c.str[i]==' ')continue;
      float left=-total/2+i*adv-hudfont::OriginX*scale,top=-hudfont::DigitCenterY*scale;
      float right=left+hudfont::Width*scale,bottom=top+hudfont::Height*scale;
      float l=1e6f,t=1e6f,r=-1e6f,b=-1e6f;
      for(float u:{left,right})for(float v:{top,bottom}){float x=c.x+c.cs*u-c.sn*v,y=c.y+c.sn*u+c.cs*v;l=std::min(l,x);r=std::max(r,x);t=std::min(t,y);b=std::max(b,y);}
      int x0=std::max(rect.x,int(std::floor(std::max(l,c.clip.l)))),x1=std::min(rect.x+rect.width-1,int(std::ceil(std::min(r,c.clip.r))));
      int y0=std::max(rect.y,int(std::floor(std::max(t,c.clip.t)))),y1=std::min(rect.y+rect.height-1,int(std::ceil(std::min(b,c.clip.b))));
      for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){float dx=x-c.x,dy=y-c.y,u=(c.cs*dx+c.sn*dy-left)*inverseScale,v=(-c.sn*dx+c.cs*dy-top)*inverseScale;
        float alpha=0;if(c.weight<1)alpha+=sample(hudfont::glyphs[0][index],u,v)*(1-c.weight);if(c.weight>0)alpha+=sample(hudfont::glyphs[1][index],u,v)*c.weight;
        if(alpha>0)plot(x,y,c.green,alpha*(1.f/255.f),c);
      }
    }
  }
};
}
using namespace huddetail;
struct HudRenderer::State {List slots[3];Wheel wheels[2];uint32_t last=0;bool started=false;};
HudRenderer::~HudRenderer(){if(state_){state_->~State();std::free(state_);}}
bool HudRenderer::begin(){if(state_)return true;
#ifdef ARDUINO
  void* memory=heap_caps_malloc(sizeof(State),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
#else
  void* memory=std::malloc(sizeof(State));
#endif
  if(memory)state_=new(memory) State();return state_!=nullptr;
}
bool HudRenderer::prepareFrame(unsigned slot,const HudInput& input){
  if(!state_||slot>=3)return false;
  auto& list=state_->slots[slot];list.count=0;list.valid=false;
  const float dt=state_->started?std::min(.2f,uint32_t(input.nowMs-state_->last)*.001f):0;state_->last=input.nowMs;state_->started=true;
  float values[2]={bound(safe(input.speedKmh),0,200),bound(safe(input.altitudeM),0,3000)};
  for(int i=0;i<2;++i){auto& w=state_->wheels[i];if(!w.started){w.from=w.to=int(std::round(values[i]));w.started=true;}
    const bool resting=w.elapsed>=CounterCycle;
    const float elapsed=w.elapsed+dt;
    w.elapsed=std::min(CounterCycle,elapsed);
    if(elapsed>=CounterCycle&&std::fabs(values[i]-w.to)>=.65f){
      w.from=w.to;w.to=int(std::round(values[i]));
      // Consume the frame interval immediately, and carry overshoot into the
      // next detent. A new target never restarts an in-flight digit movement.
      w.elapsed=std::min(CounterCycle,resting?dt:elapsed-CounterCycle);
    }}
  Builder b(list);float r=radius(),tickPitch=r*Pi/18;
  heading(b,wrap(input.heading),tickPitch);tape(b,state_->wheels[0],true,values[0],tickPitch);tape(b,state_->wheels[1],false,values[1],tickPitch);
  const bool valid=input.attitudeValid&&std::isfinite(input.pitch)&&std::isfinite(input.roll);
  const float trueRoll=std::remainder(safe(input.roll),360.f);
  float roll=bound(trueRoll,-60,60);
  if(valid)ladder(b,bound(input.pitch,-90,90),trueRoll);else b.text("IMU",233,280,12,255);
  b.ring(233,234,14.f);b.ring(233,234,7.5f);b.line(195,234,219,234,1.6f,255);b.line(247,234,271,234,1.6f,255);b.line(233,220,233,206,1.6f,255);
  bank(b,roll,valid,r);
  char s[12];std::snprintf(s,sizeof(s),"%+.1f",bound(safe(input.climb),-15,15));b.text(s,398,368,14);b.text("V/S",398,382,12);
  list.valid=list.count<Capacity;return list.valid;
}
void HudRenderer::renderPrepared(uint16_t* pixels,unsigned slot,AirspeedRect rect)const{
  if(!pixels||!state_||slot>=3||rect.x<0||rect.y<0||rect.width<=0||rect.height<=0||rect.x>466-rect.width||rect.y>466-rect.height)return;
  std::memset(pixels,0,size_t(rect.width)*rect.height*2);const auto& list=state_->slots[slot];if(!list.valid)return;
  Raster raster{pixels,rect,wireOrder_};for(int i=0;i<list.count;++i){const auto& c=list.items[i];if(c.kind==Command::Text)raster.text(c);else raster.shape(c);}
}
void HudRenderer::render(uint16_t* pixels,const HudInput& input){if(prepareFrame(0,input))renderPrepared(pixels,0,{0,0,466,466});}
AltimeterDamage HudRenderer::damage(){AltimeterDamage d;d.count=7;
  d.regions[0]={118,30,230,66};
  // Split the busy central area evenly between the two raster workers.
  for(int i=0;i<4;++i)d.regions[i+1]={0,96+i*64,466,64};
  d.regions[5]={116,352,330,44};d.regions[6]={116,396,234,64};return d;}
}
