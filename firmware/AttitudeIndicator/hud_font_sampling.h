#pragma once
#include "hud_font.h"

namespace instrument { namespace hudfont {
// Sample the Orbitron Bold bitmap, interpolating its antialiased coverage.
inline __attribute__((always_inline)) float sampleCell(const uint8_t* glyph,int offset,float fx,float fy){
  const auto* p=glyph+offset;
  const uint8_t topLeft=p[0],topRight=p[1],bottomLeft=p[Width],bottomRight=p[Width+1];
  if(!(topLeft|topRight|bottomLeft|bottomRight))return 0;
  return (topLeft*(1-fx)+topRight*fx)*(1-fy)+(bottomLeft*(1-fx)+bottomRight*fx)*fy;
}
inline __attribute__((always_inline)) float sampleGlyph(const uint8_t* glyph,float x,float y){
  if(x<0||y<0||x>=Width-1||y>=Height-1)return 0;
  const int ix=int(x),iy=int(y);return sampleCell(glyph,iy*Width+ix,x-ix,y-iy);
}
} }
