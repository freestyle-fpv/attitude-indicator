#include "instrument_controls.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using instrument::InstrumentControls;
static void command(InstrumentControls& c,const char* text,uint32_t now){while(*text)c.input(*text++,now);}
static void hudDemoState(){
  InstrumentControls c;
  assert(c.input('u',100)==InstrumentControls::DisplayChanged&&c.hud()&&!c.demo());
  assert(c.input('u',100)==InstrumentControls::None);
  assert(c.input('d',200)==InstrumentControls::DisplayChanged&&c.demo()&&c.demoStarted()==200);
  command(c,"h",300);assert(c.attitude()&&!c.demo());
  c.toggleDemo(400);assert(c.demo()&&c.demoStarted()==400);
  command(c,"u",500);assert(c.hud()&&c.demo()&&c.demoStarted()==200);
  c.toggleDemo(600);assert(!c.demo()&&c.demoStarted()==600);
  command(c,"h",700);assert(c.demo()&&c.demoStarted()==400);
  command(c,"a",800);assert(c.demo()&&c.demoStarted()==0);
  command(c,"t",900);assert(c.demo()&&c.demoStarted()==0);
  command(c,"u",1000);c.toggleDemo(1100);assert(c.demo());
  assert(c.input('c',1200)==InstrumentControls::Calibrate&&c.hud()&&!c.demo());
  command(c,"h",1300);assert(!c.demo());
  command(c,"a",1400);assert(c.demo());
  command(c,"t",1500);assert(c.demo());
  command(c,"u",1600);assert(c.hud()&&!c.demo());
  const float speed=c.speed(),altitude=c.altitude();
  c.update(1620);assert(c.speed()!=speed&&c.altitude()!=altitude);
}
static void climbRate(){
  InstrumentControls c;
  assert(c.climb()==0);
  command(c,"b276\n",0);
  c.update(1000);
  // The altitude moves about 0.753 m over a real one-second interval,
  // even though the smoothing step is capped at 100 ms.
  assert(c.climb()>.74f&&c.climb()<.77f);
  c.update(1000);assert(std::isfinite(c.climb()));
  command(c,"b3000\n",1000);c.update(1001);assert(c.climb()==15);
  command(c,"b0\n",1001);c.update(1002);assert(c.climb()==-15);
  for(uint32_t now=1022;now<5022;now+=20){
    c.update(now);assert(std::isfinite(c.climb())&&std::fabs(c.climb())<=15);
  }
  assert(c.altitude()==0&&c.climb()==0);
  command(c,"b100\n",0xfffffff0u);c.update(20);
  assert(std::isfinite(c.climb())&&c.climb()==15);
  command(c,"u",20);assert(c.hud());
}
int main(){
  // A calibration click must preserve the selected dial and clear both
  // attitude demo flags so every attitude consumer uses the shared sensor.
  {InstrumentControls c;c.input('h',0);c.toggleDemo(1);c.input('u',2);c.toggleDemo(3);
    for(int i=0;i<4;++i){c.step(1);const bool a=c.airspeed(),h=c.hud(),t=c.altimeter();
      assert(c.shortPress()==InstrumentControls::Calibrate);
      assert(a==c.airspeed()&&h==c.hud()&&t==c.altimeter());}
    c.input('h',4);assert(!c.demo());c.input('u',5);assert(!c.demo());}
  hudDemoState();
  climbRate();
  InstrumentControls c;
  assert(c.attitude()&&!c.demo());
  assert(c.step(0)==InstrumentControls::None&&c.attitude());
  c.step(1);assert(c.hud()&&!c.demo());
  c.step(1);assert(c.airspeed()&&c.demo());
  c.step(1);assert(c.altimeter()&&c.demo());
  c.step(1);assert(c.attitude()&&!c.demo());
  c.step(-1);assert(c.altimeter()&&c.demo());
  c.step(-1);assert(c.airspeed()&&c.demo());
  c.step(-1);assert(c.hud()&&!c.demo());
  c.step(-1);assert(c.attitude()&&!c.demo());
  for(uint32_t now=0;now<120000;now+=17){
    c.update(now);assert(std::isfinite(c.speed())&&c.speed()>=0&&c.speed()<=200);
    assert(std::isfinite(c.altitude())&&c.altitude()>=125&&c.altitude()<=425);
  }
  c.toggleDemo(120000);assert(c.demo());
  c.step(-1);assert(c.altimeter()&&c.demo());
  command(c,"b275\n",120000);
  for(int i=0;i<100;++i)c.update(120000+i*20);
  assert(c.altimeter()&&!c.demo()&&std::fabs(c.altitude()-275)<.01f);
  c.step(1);assert(c.attitude()&&c.demo());
  c.step(1);assert(c.hud()&&!c.demo());
  c.step(1);assert(c.airspeed()&&c.demo());
  command(c,"v250\n",120001);c.update(121001);assert(!c.demo());
  for(int i=0;i<100;++i)c.update(121001+i*20);assert(std::fabs(c.speed()-200)<.01f);
  command(c,"v-20\r\n",124000);for(int i=0;i<100;++i)c.update(124000+i*20);assert(c.speed()<.01f);
  command(c,"vnan\nv1e99\nv120garbage\nv123456789012345678901234567890\n",127000);
  c.update(128000);assert(c.speed()<.01f); // Malformed and overflowed records cannot create a speed.
  command(c,"v120\n",129000);for(int i=0;i<100;++i)c.update(129000+i*20);assert(std::fabs(c.speed()-120)<.01f);
  command(c,"h",132000);assert(!c.airspeed()&&c.demo());
  assert(c.input('c',132000)==InstrumentControls::Calibrate);assert(!c.demo());
  command(c,"a",132000);assert(c.airspeed()&&!c.demo());
  c.toggleDemo(0xfffffff0u);c.update(0xfffffff0u);c.update(20);assert(c.speed()>=0&&c.speed()<=200);
  command(c,"t",100);assert(c.altimeter()&&!c.demo());
  command(c,"b3001\n",100);for(int i=0;i<150;++i)c.update(100+i*20);assert(c.altitude()==3000);
  command(c,"b2999.9\n",3100);for(int i=0;i<100;++i)c.update(3100+i*20);assert(std::fabs(c.altitude()-2999.9f)<.001f);
  command(c,"b-123\n",3200);for(int i=0;i<150;++i)c.update(3200+i*20);assert(c.altitude()==0);
  command(c,"bnan\nbinf\nb20bad\nb123456789012345678901234567890\n",6400);c.update(6420);assert(c.altitude()==0);
  c.toggleDemo(0xfffffff0u);c.update(0xfffffff0u);c.update(20);
  assert(std::isfinite(c.altitude())&&c.altitude()>=0&&c.altitude()<=3000);
  command(c,"a",25);assert(c.airspeed()&&c.demo());
  command(c,"t",30);assert(c.altimeter()&&c.demo());
  std::puts("PASS four-mode selection, independent HUD demo/timing, serial validation, climb rate, clamps and timer wrap");
}
