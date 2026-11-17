#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdlib>
namespace instrument {
struct TouchReport {int x=0,y=0;uint8_t contacts=0,id=0;};
// CST9217 D000 report format; exact byte count and controller marker required.
inline bool decodeTouch(const uint8_t* data,size_t size,TouchReport& out){
  if(!data||size!=15||data[6]!=0xAB)return false;
  const uint8_t count=data[5]&0x7f;if(count>2)return false;
  out=TouchReport();out.contacts=(data[4]&0xf0)?2:count;
  if(out.contacts!=1)return true;
  if((data[0]&0xf)!=6){out.contacts=0;return true;}
  out.id=data[0]>>4;if(out.id>1)return false;
  out.x=(int(data[1])<<4)|(data[3]>>4);out.y=(int(data[2])<<4)|(data[3]&15);
  return out.x<466&&out.y<466;
}
class VerticalSwipe {
 public:
  // +1 upward -> next, -1 downward -> previous. One action per contact.
  int update(const TouchReport& point,uint32_t now){
    if(point.contacts==0){active_=blocked_=false;return 0;}
    if(point.contacts!=1){cancel();blocked_=true;return 0;}
    if(blocked_)return 0;
    if(!active_){active_=true;startX_=point.x;startY_=point.y;started_=last_=now;id_=point.id;return 0;}
    if(point.id!=id_||uint32_t(now-last_)>200||uint32_t(now-started_)>1200){cancel();return 0;}
    last_=now;
    const int dx=point.x-startX_,dy=point.y-startY_;
    if(std::abs(dx)>64&&std::abs(dx)>std::abs(dy)){cancel();return 0;}
    if(std::abs(dy)>=72&&std::abs(dy)>=2*std::abs(dx)&&uint32_t(now-started_)>=30){
      blocked_=true;return dy<0?1:-1;
    }
    return 0;
  }
  void cancel(){blocked_=blocked_||active_;active_=false;}
 private:
  bool active_=false,blocked_=false;uint8_t id_=0;
  int startX_=0,startY_=0;uint32_t started_=0,last_=0;
};
class TouchTap {
 public:
  bool update(const TouchReport& point,uint32_t now){
    if(!point.contacts){
      const bool tapped=active_&&!blocked_&&uint32_t(now-started_)>=30&&uint32_t(now-started_)<=650;
      active_=blocked_=false;return tapped;
    }
    if(point.contacts!=1)cancel();
    if(blocked_)return false;
    if(!active_){start=point;started_=now;active_=true;}
    else if(point.id!=start.id||std::abs(point.x-start.x)>20||std::abs(point.y-start.y)>20)cancel();
    return false;
  }
  void cancel(){blocked_=true;active_=false;}
  TouchReport start;
 private:
  bool active_=false,blocked_=false;
  uint32_t started_=0;
};
}
