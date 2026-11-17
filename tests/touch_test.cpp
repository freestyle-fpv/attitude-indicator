#include "touch_gesture.h"
#include "board_config.h"
#include "instrument_settings.h"
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
  InstrumentControls c;assert(c.attitude());c.step(-1);assert(c.altimeter());c.step(1);assert(c.attitude());
  for(int i=0;i<4;++i)c.step(1);assert(c.attitude());for(int i=0;i<4;++i)c.step(-1);assert(c.attitude());
  assert(c.step(0)==InstrumentControls::None);
  VerticalSwipe mounted;
  const auto swipeTo=[&](int rawY,uint32_t ms){
    TouchReport report;report.contacts=1;report.x=232;report.y=board::touchScreenY(rawY);
    c.step(mounted.update(report,ms));
  };
  // On the mounted board, raw Y increases towards the top of the screen.
  for(int dial=0;dial<4;++dial){
    swipeTo(110,4000+dial*200);swipeTo(285,4100+dial*200);
    assert(dial==0?c.hud():dial==1?c.airspeed():dial==2?c.altimeter():c.attitude());
    mounted.update(TouchReport(),4150+dial*200);
  }
  swipeTo(285,5000);swipeTo(110,5100);assert(c.altimeter());
  SettingsMenu menu;
  assert(menu.options.fps && menu.options.side==UsbSide::Left);
  menu.click();assert(menu.page==SettingsPage::Main);
  assert(menu.tap(230,283) && !menu.options.fps);
  assert(menu.tap(230,214) && menu.page==SettingsPage::Direction);
  assert(menu.tap(230,221) && menu.options.side==UsbSide::Top && menu.page==SettingsPage::Main);
  menu.click();assert(menu.page==SettingsPage::Closed);
  const auto saved=DisplayOptions::decode(menu.options.encode());
  assert(saved.side==UsbSide::Top && !saved.fps);
  for(auto side:{UsbSide::Left,UsbSide::Top,UsbSide::Right,UsbSide::Bottom}){
    const ScreenPoint panel=toPanel({213,378},side);
    const auto logical=board::touchPoint(465-panel.x,465-panel.y,side);
    assert(logical.x==213 && logical.y==378);
  }
  const uint16_t original[]={1,2,3,4,5,6};
  const uint16_t upright[4][6]={{1,2,3,4,5,6},{2,4,6,1,3,5},{6,5,4,3,2,1},{5,3,1,6,4,2}};
  for(unsigned i=0;i<4;++i){
    uint16_t output[6]={};
    copyPanelPixels(output,original,2,3,UsbSide(i),0,2);
    copyPanelPixels(output+2,original,2,3,UsbSide(i),2,4);
    assert(std::equal(output,output+6,upright[i]));
    const Vec3 downSensors[]={{-1,0,0},{0,1,0},{1,0,0},{0,-1,0}};
    const auto a=board::toBody(downSensors[i],UsbSide(i));
    assert(a.x==0 && a.y==0 && a.z==1);
  }
  TouchTap tap;
  TouchReport finger;finger.contacts=1;finger.x=232;finger.y=220;
  tap.update(finger,6000);assert(tap.update(TouchReport(),6070));
  tap.update(finger,6100);finger.y+=90;tap.update(finger,6150);assert(!tap.update(TouchReport(),6200));
  std::puts("PASS CST9217 packet validation, vertical gesture filtering/latching, time wrap and four-mode cycling");
}
