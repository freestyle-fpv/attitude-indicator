#include "circle_band.h"
#include "fps_overlay.h"
#include "renderer.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
const int N=466;
const uint16_t Guard=0xa53c;

// Independent geometric oracle: enumerate every pixel in each row using the
// panel's original floating-point center/radius, without using helper bounds.
bool insideCircle(int x,int y){
  const double dx=x-232.5,dy=y-232.5;
  return dx*dx+dy*dy<=232.5*232.5;
}

void testAllBounds(){
  int left[N],right[N];
  for(int y=0;y<N;++y){
    left[y]=N;right[y]=-1;
    for(int x=0;x<N;++x)if(insideCircle(x,y)){
      left[y]=std::min(left[y],x);right[y]=std::max(right[y],x);
    }
  }
  size_t bands=0;
  for(int first=0;first<N;++first){
    int visibleLeft=N,visibleRight=-1;
    for(int rows=1;rows<=N-first;++rows){
      visibleLeft=std::min(visibleLeft,left[first+rows-1]);
      visibleRight=std::max(visibleRight,right[first+rows-1]);
      const instrument::CircleBand band=instrument::circleBandBounds(first,rows);
      assert(band.x>=0 && band.width>=2 && band.width<=N);
      assert((band.x&1)==0 && (band.width&1)==0 && band.x<=N-band.width);
      if(visibleRight>=0){
        assert(band.x<=visibleLeft && band.x+band.width>visibleRight);
      }
      ++bands;
    }
  }
  // Cropping must actually reduce transfer size away from the equator.
  assert(instrument::circleBandBounds(0,1).width<N);
  assert(instrument::circleBandBounds(0,118).width<N);
  assert(instrument::circleBandBounds(348,118).width<N);
  assert(instrument::circleBandBounds(0,N).width==N);
  const int invalid[][2]={{-1,1},{N,1},{N+1,1},{0,0},{0,-1},
    {0,N+1},{N-1,2},{INT_MAX,1},{0,INT_MAX},{INT_MIN,INT_MAX},
    {INT_MAX,INT_MAX},{INT_MIN,INT_MIN}};
  for(const auto& range:invalid){
    const instrument::CircleBand band=instrument::circleBandBounds(range[0],range[1]);
    assert(band.x==0 && band.width==0);
  }
  std::printf("Circle bounds: all %zu valid bands cover every visible pixel; alignment and invalid ranges PASS\n",bands);
  std::fflush(stdout);
}

void checkPacking(const std::vector<uint16_t>& original,int first,int rows,
                  instrument::CircleBand band,int offset){
  const size_t sourcePixels=static_cast<size_t>(N)*rows;
  const size_t payloadPixels=static_cast<size_t>(band.width)*rows;
  std::vector<uint16_t> guarded(sourcePixels+offset+16,Guard);
  uint16_t* pixels=guarded.data()+offset;
  const uint16_t* source=original.data()+first*N;
  std::copy(source,source+sourcePixels,pixels);
  assert(instrument::packCircleBandInPlace(pixels,rows,band));
  // memcmp catches byte swaps and compares to untouched original coordinates.
  for(int row=0;row<rows;++row)
    assert(std::memcmp(pixels+row*band.width,source+row*N+band.x,
                       band.width*sizeof(uint16_t))==0);
  // Packing may modify only its destination payload, not the unused tail.
  assert(std::memcmp(pixels+payloadPixels,source+payloadPixels,
                     (sourcePixels-payloadPixels)*sizeof(uint16_t))==0);
  for(int i=0;i<offset;++i)assert(guarded[i]==Guard);
  for(size_t i=offset+sourcePixels;i<guarded.size();++i)assert(guarded[i]==Guard);
}

void testPacking(){
  std::vector<uint16_t> original(N*N);
  uint32_t state=0x865dc39b;
  for(uint16_t& pixel:original){
    state=state*1664525u+1013904223u;
    pixel=static_cast<uint16_t>(state>>8);
  }
  // Every starting row, including clipped last bands and both uint16_t/word
  // alignment; short rows exercise the most overlapping memmove calls.
  const int heights[]={1,2,3,78,94,118,156,466};
  size_t cases=0;
  for(int first=0;first<N;++first)for(int height:heights){
    const int rows=std::min(height,N-first);
    checkPacking(original,first,rows,instrument::circleBandBounds(first,rows),1+(cases&1));
    ++cases;
  }
  for(int i=0;i<512;++i){
    state=state*1664525u+1013904223u;
    const int first=state%N;
    state=state*1664525u+1013904223u;
    const int rows=1+state%(N-first);
    checkPacking(original,first,rows,instrument::circleBandBounds(first,rows),1+(cases&1));
    ++cases;
  }
  // A legal rectangle can be asymmetric. Test extreme left/right strips,
  // overlapping within a row, multiple rows, and full-width no-op handling.
  const instrument::CircleBand rectangles[]={{0,2},{464,2},{2,464},{0,464},{0,N}};
  for(const auto& band:rectangles)for(int rows:heights){
    checkPacking(original,0,rows,band,1+(cases&1));++cases;
  }
  std::vector<uint16_t> unchanged(2*N+16,Guard);
  uint16_t* target=unchanged.data()+2;
  const instrument::CircleBand valid={0,N};
  assert(!instrument::packCircleBandInPlace(nullptr,1,valid));
  for(int rows:{INT_MIN,-1,0,N+1,INT_MAX})
    assert(!instrument::packCircleBandInPlace(target,rows,valid));
  const instrument::CircleBand invalid[]={{-2,2},{0,0},{0,-2},{0,1},{1,2},
    {0,N+2},{2,N},{N,2},{INT_MIN,2},{INT_MAX,2},{0,INT_MAX},
    {INT_MAX,INT_MAX},{INT_MIN,INT_MIN}};
  for(const auto& band:invalid)
    assert(!instrument::packCircleBandInPlace(target,2,band));
  for(uint16_t pixel:unchanged)assert(pixel==Guard);
  std::printf("Circle packing: %zu payloads preserve bytes and guards; overlap, unaligned words, no-op and invalid inputs PASS\n",cases);
  std::fflush(stdout);
}

void testRenderedFrames(){
  const int heights[]={118,94,78,156};
  const float poses[][2]={{0,0},{-90,-180},{90,180},{-89.999f,89.999f},
    {89.999f,-89.999f},{-17.375f,31.125f},{3.14159f,-127.875f},
    {42.625f,716.75f},{-103.75f,-923.125f}};
  std::vector<uint16_t> full(N*N),reconstructed(N*N),guarded(N*156+20);
  size_t cases=0;
  for(bool wireOrder:{false,true}){
    instrument::Renderer renderer(wireOrder);
    instrument::FpsOverlay overlay(wireOrder);
    assert(renderer.begin());
    for(const auto& pose:poses)for(int height:heights){
      renderer.render(full.data(),pose[0],pose[1]);
      overlay.update(cases%5==0?-1:cases*.37f);
      overlay.blit(full.data(),N);
      overlay.blitDemoBand(full.data(),N,0,N);
      std::fill(reconstructed.begin(),reconstructed.end(),0);
      for(int first=0;first<N;first+=height){
        const int rows=std::min(height,N-first),offset=1+(cases&1);
        std::fill(guarded.begin(),guarded.end(),Guard);
        uint16_t* pixels=guarded.data()+offset;
        renderer.renderBand(pixels,pose[0],pose[1],first,rows);
        const instrument::CircleBand band=instrument::circleBandBounds(first,rows);
        assert(instrument::packCircleBandInPlace(pixels,rows,band));
        // The compact target uses global column band.x at memory column zero.
        overlay.blitBand(pixels,band.width,first,rows,band.x);
        overlay.blitDemoBand(pixels,band.width,first,rows,band.x);
        for(int row=0;row<rows;++row)
          std::copy(pixels+row*band.width,pixels+(row+1)*band.width,
                    reconstructed.begin()+(first+row)*N+band.x);
        for(int i=0;i<offset;++i)assert(guarded[i]==Guard);
        for(size_t i=offset+N*rows;i<guarded.size();++i)assert(guarded[i]==Guard);
      }
      // Full equality catches omitted edges/text and any payload row errors.
      assert(full==reconstructed);
      for(int y=0;y<N;++y)for(int x=0;x<N;++x)
        if(!insideCircle(x,y))assert(reconstructed[y*N+x]==0);
      ++cases;
    }
  }
  std::printf("Circle reconstruction: %zu real native/wire frames with FPS/DEMO match exactly at heights 118/94/78/156 PASS\n",cases);
}
}

int main(){
  testAllBounds();
  testPacking();
  testRenderedFrames();
}
