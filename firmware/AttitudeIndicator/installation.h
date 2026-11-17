#pragma once
#include "airspeed_renderer.h"
#include <algorithm>
#include <cstring>

namespace instrument {
enum class UsbSide : uint8_t { Left, Top, Right, Bottom };
struct ScreenPoint { int x,y; };
constexpr int ScreenSize=466;

inline UsbSide inverse(UsbSide side){return UsbSide((4-unsigned(side))&3);}
inline bool quarterTurn(UsbSide side){return side==UsbSide::Top||side==UsbSide::Bottom;}
// Logical upright coordinates to the panel's original USB-left coordinates.
inline ScreenPoint toPanel(ScreenPoint p,UsbSide side){
  constexpr int last=ScreenSize-1;
  switch(side){
    case UsbSide::Top:return {p.y,last-p.x};
    case UsbSide::Right:return {last-p.x,last-p.y};
    case UsbSide::Bottom:return {last-p.y,p.x};
    default:return p;
  }
}
inline AirspeedRect panelRect(AirspeedRect r,UsbSide side){
  switch(side){
    case UsbSide::Top:return {r.y,ScreenSize-r.x-r.width,r.height,r.width};
    case UsbSide::Right:return {ScreenSize-r.x-r.width,ScreenSize-r.y-r.height,r.width,r.height};
    case UsbSide::Bottom:return {ScreenSize-r.y-r.height,r.x,r.height,r.width};
    default:return r;
  }
}

// Destination order is physical scan order. Offsets are in the rotated output,
// so a DMA chunk can start or finish in the middle of a row.
inline void copyPixels(uint16_t* dst,const uint16_t* src,size_t count){
  if((reinterpret_cast<uintptr_t>(dst)|reinterpret_cast<uintptr_t>(src))&3){
    std::memcpy(dst,src,count*sizeof(uint16_t));return;
  }
  typedef uint32_t Word __attribute__((__may_alias__));
  auto* out=reinterpret_cast<Word*>(dst);
  const auto* in=reinterpret_cast<const Word*>(src);
  size_t words=count/2;
  for(;words>=8;words-=8,out+=8,in+=8){
    const Word a=in[0],b=in[1],c=in[2],d=in[3],e=in[4],f=in[5],g=in[6],h=in[7];
    out[0]=a;out[1]=b;out[2]=c;out[3]=d;out[4]=e;out[5]=f;out[6]=g;out[7]=h;
  }
  for(;words;--words)*out++=*in++;
  if(count&1)*reinterpret_cast<uint16_t*>(out)=*reinterpret_cast<const uint16_t*>(in);
}
inline void copyPanelPixels(uint16_t* dst,const uint16_t* src,int width,int height,
                            UsbSide side,int offset,int count){
  if(side==UsbSide::Left){copyPixels(dst,src+offset,count);return;}
  if(side==UsbSide::Right){
    int from=width*height-1-offset;
    for(int i=0;i<count;++i)dst[i]=src[from-i];
    return;
  }
  const bool top=side==UsbSide::Top;
  const int step=top?width:-width;
  int row=offset/height,column=offset%height;
  while(count>0){
    const int pixels=std::min(count,height-column);
    int from=top?column*width+width-1-row:(height-1-column)*width+row;
    for(int i=0;i<pixels;++i,from+=step)*dst++=src[from];
    count-=pixels;column=0;++row;
  }
}
}
