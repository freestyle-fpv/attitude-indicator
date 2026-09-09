#pragma once
#include <cstdint>
namespace instrument {
struct CircleBand { int x; int width; };
// Bound all visible pixels in a 466-wide band of the circle centered at
// (232.5,232.5), radius 232.5. Invalid ranges return an empty rectangle.
// Even x/width preserve 4-byte offsets and row lengths for a DMA-aligned base.
inline CircleBand circleBandBounds(int firstRow,int rows){
  const int size=466;
  if(firstRow<0 || firstRow>=size || rows<=0 || rows>size-firstRow)return {0,0};
  const int lastRow=firstRow+rows-1;
  // The row nearest the equator has the widest span of the entire band.
  const int nearRow=firstRow>232?firstRow:(lastRow<232?lastRow:232);
  const int dy=2*nearRow-465;
  const int remaining=465*465-dy*dy;
  int left=0,right=232;
  // Find the first inside pixel in at most eight integer comparisons. The
  // pole rows contain no inside pixels; their conservative result is [232,234).
  while(left<right){
    const int middle=left+(right-left)/2;
    const int dx=465-2*middle;
    if(dx*dx<=remaining)right=middle;
    else left=middle+1;
  }
  const int x=left&~1;
  return {x,size-2*x};
}
// Compact rows from stride 466 to band.width at the start of pixels. The
// caller supplies storage for rows*466 uint16_t pixels. Native/wire bytes are
// copied unchanged. Invalid inputs return false without writing; full width
// is a successful no-op. Storage after rows*band.width pixels is untouched.
inline bool packCircleBandInPlace(uint16_t* pixels,int rows,CircleBand band){
  const int size=466;
  if(!pixels || rows<=0 || rows>size || band.x<0 || band.width<2 ||
     band.width>size || band.x>size-band.width || (band.x&1) || (band.width&1))return false;
  if(band.width==size)return true;
  // Copy forward: destination never follows source, and its end cannot reach
  // the next source row. Explicit word loads/stores avoid slow generic memory
  // moves on ESP32. Even x, width and stride keep every row word-aligned.
#if defined(__GNUC__) || defined(__clang__)
  if((reinterpret_cast<uintptr_t>(pixels)&3u)==0){
    typedef uint32_t AliasWord __attribute__((__may_alias__));
    AliasWord* words=reinterpret_cast<AliasWord*>(pixels);
    const int widthWords=band.width/2,xWords=band.x/2;
    for(int row=0;row<rows;++row){
      AliasWord* target=words+row*widthWords;
      const AliasWord* source=words+row*(size/2)+xWords;
      for(int word=0;word<widthWords;++word)target[word]=source[word];
    }
    return true;
  }
#endif
  // Portable fallback also supports buffers aligned to only uint16_t.
  for(int row=0;row<rows;++row){
    uint16_t* target=pixels+row*band.width;
    const uint16_t* source=pixels+row*size+band.x;
    for(int pixel=0;pixel<band.width;++pixel)target[pixel]=source[pixel];
  }
  return true;
}
}
