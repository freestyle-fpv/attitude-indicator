#include "fps_overlay.h"
#include <cmath>
#include <cstdio>
#include <cstring>
namespace instrument {
namespace {
// Five columns per glyph, bit 0 at the top. Generated UI uses no bitmap asset.
const uint8_t digits[10][5]={
 {0x3e,0x51,0x49,0x45,0x3e},{0,0x42,0x7f,0x40,0},
 {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4b,0x31},
 {0x18,0x14,0x12,0x7f,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3c,0x4a,0x49,0x49,0x30},{1,0x71,9,5,3},
 {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1e}};
const uint8_t f[5]={0x7f,9,9,9,1},p[5]={0x7f,9,9,9,6},s[5]={0x46,0x49,0x49,0x49,0x31};
const uint8_t d[5]={0x7f,0x41,0x41,0x22,0x1c},e[5]={0x7f,0x49,0x49,0x49,0x41};
const uint8_t m[5]={0x7f,2,4,2,0x7f},o[5]={0x3e,0x41,0x41,0x41,0x3e};
const uint8_t dot[5]={0,0x60,0x60,0,0},dash[5]={8,8,8,8,8},space[5]={};
const uint8_t* glyph(char c){
 if(c>='0'&&c<='9')return digits[c-'0'];
 switch(c){case 'F':return f;case 'P':return p;case 'S':return s;case 'D':return d;case 'E':return e;case 'M':return m;case 'O':return o;case '.':return dot;case '-':return dash;default:return space;}
}
}
void FpsOverlay::update(float fps){
  const int tenths=std::isfinite(fps)&&fps>=0?static_cast<int>(std::fmin(fps,99.9f)*10+.5f):-1;
  if(tenths==lastTenths_)return;
  lastTenths_=tenths;
  char text[16];
  if(tenths<0)std::snprintf(text,sizeof(text),"--.- FPS");
  else std::snprintf(text,sizeof(text),"%2d.%d FPS",tenths/10,tenths%10);
  for(int i=0;i<Width*Height;++i)pixels_[i]=0x0841;
  for(int i=0;i<8;++i){
    const uint8_t* bits=glyph(text[i]);
    for(int x=0;x<5;++x)for(int y=0;y<7;++y)if(bits[x]&(1u<<y))
      for(int sy=0;sy<2;++sy)for(int sx=0;sx<2;++sx)
        pixels_[(4+2*y+sy)*Width+4+i*12+2*x+sx]=0xffff;
  }
  if(wireOrder_)for(int i=0;i<Width*Height;++i)pixels_[i]=static_cast<uint16_t>((pixels_[i]<<8)|(pixels_[i]>>8));
}
void FpsOverlay::blit(uint16_t* frame,int stride) const{
  blitBand(frame,stride,0,Y+Height);
}
void FpsOverlay::blitBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn) const{
  if(!frame || firstColumn<0 || firstColumn>X || stride<X+Width-firstColumn || firstRow<0 || rowCount<=0 || firstRow>=Y+Height)return;
  const int first=firstRow>Y?firstRow:Y;
  // Subtract before comparing so even an excessive rowCount cannot overflow.
  for(int y=first;y<Y+Height && y-firstRow<rowCount;++y)
    std::memcpy(frame+(static_cast<size_t>(y-firstRow)*stride+X-firstColumn),pixels_+(y-Y)*Width,Width*sizeof(uint16_t));
}
void FpsOverlay::blitDemoBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn) const{
  const int xOrigin=209,yOrigin=414;
  if(!frame || firstColumn<0 || firstColumn>xOrigin || stride<xOrigin+48-firstColumn || firstRow<0 || rowCount<=0 || firstRow>=yOrigin+14)return;
  const uint16_t yellow=wireOrder_?0xe0ff:0xffe0;
  const int first=firstRow>yOrigin?firstRow:yOrigin;
  for(int y=first;y<yOrigin+14 && y-firstRow<rowCount;++y){
    const int bit=(y-yOrigin)/2;
    uint16_t* row=frame+(static_cast<size_t>(y-firstRow)*stride+xOrigin-firstColumn);
    for(int letter=0;letter<4;++letter){
      const uint8_t* bits=glyph("DEMO"[letter]);
      for(int x=0;x<5;++x)if(bits[x]&(1u<<bit)){
        row[letter*12+x*2]=yellow;row[letter*12+x*2+1]=yellow;
      }
    }
  }
}
}
