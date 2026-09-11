#include "touch_gesture.h"
#include "instrument_controls.h"
#include <cassert>
#include <cstdio>
using namespace instrument;
int main(){
  TouchReport p;uint8_t bytes[15]={};bytes[6]=0xAB;bytes[5]=1;bytes[0]=6;bytes[1]=12;bytes[2]=18;bytes[3]=0x34;
  assert(decodeTouch(bytes,15,p)&&p.contacts==1&&p.x==195&&p.y==292);
  assert(!decodeTouch(bytes,6,p));bytes[6]=0;assert(!decodeTouch(bytes,15,p));bytes[6]=0xAB;
  bytes[1]=0xff;assert(!decodeTouch(bytes,15,p));bytes[1]=12;
  bytes[5]=2;assert(decodeTouch(bytes,15,p)&&p.contacts==2);
  bytes[5]=0;assert(decodeTouch(bytes,15,p)&&p.contacts==0);
  VerticalSwipe s;auto point=[&](int x,int y,uint32_t ms,int count=1){TouchReport r;r.contacts=count;r.x=x;r.y=y;return s.update(r,ms);};
  assert(point(200,300,0)==0);assert(point(205,220,100)==1);
  assert(point(205,120,200)==0);assert(point(205,310,300)==0);point(0,0,320,0);
  assert(point(200,100,400)==0);assert(point(202,185,500)==-1);point(0,0,520,0);
  point(100,200,600);assert(point(210,208,700)==0);point(0,0,720,0);
  point(200,200,800);assert(point(204,195,850)==0);point(0,0,880,0);
  point(200,300,1000);point(200,200,1050,2);assert(point(200,100,1100)==0);point(0,0,1120,0);
  point(200,300,1200);assert(point(200,100,3000)==0);point(0,0,3020,0);
  point(200,300,3100);s.cancel();assert(point(200,100,3120)==0);point(0,0,3140,0);
  point(200,300,0xfffffff0u);assert(point(200,200,80)==1);
  InstrumentControls c;assert(c.airspeed());c.step(-1);assert(c.hud());c.step(1);assert(c.airspeed());
  for(int i=0;i<4;++i)c.step(1);assert(c.airspeed());for(int i=0;i<4;++i)c.step(-1);assert(c.airspeed());
  assert(c.step(0)==InstrumentControls::None);
  std::puts("PASS CST9217 packet validation, vertical gesture filtering/latching, time wrap and four-mode cycling");
}
