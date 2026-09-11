#include "fps_overlay.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
namespace instrument {
// C++11 builds may bind these constants by reference in clipping helpers.
constexpr int FpsOverlay::X;
constexpr int FpsOverlay::Y;
constexpr int FpsOverlay::Width;
constexpr int FpsOverlay::Height;
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
const uint8_t i[5]={0x41,0x41,0x7f,0x41,0x41};
const uint8_t dot[5]={0,0x60,0x60,0,0},dash[5]={8,8,8,8,8},space[5]={};
const uint8_t* glyph(char c){
 if(c>='0'&&c<='9')return digits[c-'0'];
 switch(c){case 'F':return f;case 'P':return p;case 'S':return s;case 'D':return d;case 'E':return e;case 'M':return m;case 'O':return o;case 'I':return i;case '.':return dot;case '-':return dash;default:return space;}
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
  if(!frame || firstColumn<0 || firstColumn>=X+Width || stride<=0 || firstRow<0 || rowCount<=0 || firstRow>=Y+Height)return;
  const int left=std::max(X,firstColumn),columns=std::min(stride,X+Width-firstColumn)-(left-firstColumn);
  if(columns<=0)return;
  const int first=firstRow>Y?firstRow:Y;
  // Subtract before comparing so even an excessive rowCount cannot overflow.
  for(int y=first;y<Y+Height && y-firstRow<rowCount;++y)
    std::memcpy(frame+(static_cast<size_t>(y-firstRow)*stride+left-firstColumn),pixels_+(y-Y)*Width+left-X,columns*sizeof(uint16_t));
}
void FpsOverlay::blitDemoBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn) const{
  blitLegend(frame,stride,firstRow,rowCount,firstColumn,"DEMO",4);
}
void FpsOverlay::blitSimBand(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn,bool opaque) const{
  blitLegend(frame,stride,firstRow,rowCount,firstColumn,"SIM",3,opaque);
}
void FpsOverlay::blitLegend(uint16_t* frame,int stride,int firstRow,int rowCount,int firstColumn,const char* text,int count,bool opaque) const{
  const int xOrigin=(466-count*12)/2,yOrigin=414;
  const int x0=xOrigin-(opaque?4:0),x1=xOrigin+count*12+(opaque?4:0);
  const int y0=yOrigin-(opaque?3:0),y1=yOrigin+14+(opaque?3:0);
  if(!frame || firstColumn<0 || firstColumn>=x1 || stride<=0 || firstRow<0 || rowCount<=0 || firstRow>=y1)return;
  const int left=std::max(x0,firstColumn),columns=std::min(stride,x1-firstColumn)-(left-firstColumn);
  if(columns<=0)return;
  const uint16_t yellow=wireOrder_?0xe0ff:0xffe0;
  const uint16_t backing=wireOrder_?0x4108:0x0841;
  for(int y=std::max(firstRow,y0);y<y1 && y-firstRow<rowCount;++y){
    uint16_t* row=frame+(static_cast<size_t>(y-firstRow)*stride+left-firstColumn);
    if(opaque)std::fill(row,row+columns,backing);
    if(y<yOrigin||y>=yOrigin+14)continue;
    const int bit=(y-yOrigin)/2;
    for(int letter=0;letter<count;++letter){
      const uint8_t* bits=glyph(text[letter]);
      for(int x=0;x<5;++x)if(bits[x]&(1u<<bit)){
        const int col=xOrigin+letter*12+x*2-left;
        if(col>=0&&col<columns)row[col]=yellow;
        if(col+1>=0&&col+1<columns)row[col+1]=yellow;
      }
    }
  }
}
}
