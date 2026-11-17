#include "renderer.h"
#include "circle_band.h"
#include "fps_overlay.h"
#undef INSTRUMENT_RENDERER_H
#define instrument renderer_circle_reference
#include "fixtures/background_pose_reference/renderer.h"
#undef instrument
#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <condition_variable>
#include <cstdio>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

// Build the frozen fixture separately with -Dinstrument=renderer_circle_reference.
// Wrong output strides, local/global coordinate confusion, and missing clipping
// must differ from this full-width renderer followed by the existing packer.
namespace instrument {
extern uint32_t renderStages[4];
#if defined(INSTRUMENT_TEST_POSE_CACHE)
extern size_t backgroundPoseBuilds;
#endif
}
namespace {
const int N=466,Pixels=N*N;
const uint16_t Guard=0x1234;
struct Pose {float pitch,roll;};

void checkGuards(const std::vector<uint16_t>& frame,int offset,int pixels){
  for(int i=0;i<offset;++i)assert(frame[i]==Guard);
  for(size_t i=offset+pixels;i<frame.size();++i)assert(frame[i]==Guard);
}

void packReference(const std::vector<uint16_t>& full,std::vector<uint16_t>& packed,int first,int rows){
  std::copy(full.begin()+first*N,full.begin()+(first+rows)*N,packed.begin());
  assert(instrument::packCircleBandInPlace(packed.data(),rows,instrument::circleBandBounds(first,rows)));
}

void compareBands(bool wireOrder,const std::vector<Pose>& poses){
  instrument::Renderer renderer(wireOrder);
  renderer_circle_reference::Renderer reference(wireOrder);
  assert(renderer.begin() && reference.begin());
  const int heights[]={48,60,78,94,118,156,1,N};
  std::vector<uint16_t> expected(Pixels),full(Pixels),background(Pixels),composed(Pixels);
  std::vector<uint16_t> packed(Pixels),packedBackground(Pixels),ahead(Pixels);
  for(size_t i=0;i<poses.size();++i){
    const Pose pose=poses[i],next=poses[(i+1)%poses.size()];
    reference.render(expected.data(),pose.pitch,pose.roll);
    renderer.render(full.data(),pose.pitch,pose.roll);
    assert(full==expected);
    renderer.renderBackgroundBand(background.data(),pose.pitch,pose.roll,0,N);
    for(int height:heights){
      std::fill(composed.begin(),composed.end(),0);
      for(int first=0;first<N;first+=height){
        const int rows=std::min(height,N-first),offset=1+((i+first)&1);
        const instrument::CircleBand bounds=instrument::circleBandBounds(first,rows);
        const int pixels=rows*bounds.width;
        std::vector<uint16_t> band(pixels+4,Guard);
        packReference(background,packedBackground,first,rows);
        renderer.renderBackgroundBand(band.data()+offset,pose.pitch,pose.roll,first,rows,true);
        checkGuards(band,offset,pixels);
        assert(std::equal(band.begin()+offset,band.begin()+offset+pixels,packedBackground.begin()));
        // A different pose's background must not mutate foreground's cache or
        // shared timing while the original band's foreground is still pending.
#if defined(INSTRUMENT_TEST_POSE_CACHE)
        const size_t builds=instrument::backgroundPoseBuilds;
#endif
        const uint32_t stages[]={instrument::renderStages[0],instrument::renderStages[1],
                                 instrument::renderStages[2],instrument::renderStages[3]};
        renderer.renderBackgroundBand(ahead.data(),next.pitch,next.roll,N-rows,rows,true);
        for(int stage=0;stage<4;++stage)assert(instrument::renderStages[stage]==stages[stage]);
#if defined(INSTRUMENT_TEST_POSE_CACHE)
        assert(instrument::backgroundPoseBuilds==builds);
#endif
        renderer.renderForegroundBand(band.data()+offset,pose.pitch,pose.roll,first,rows,true);
        assert(instrument::renderStages[0]==stages[0]);
#if defined(INSTRUMENT_TEST_POSE_CACHE)
        assert(instrument::backgroundPoseBuilds==builds);
#endif
        checkGuards(band,offset,pixels);
        packReference(expected,packed,first,rows);
        assert(std::equal(band.begin()+offset,band.begin()+offset+pixels,packed.begin()));
        for(int row=0;row<rows;++row)
          std::copy(band.begin()+offset+row*bounds.width,band.begin()+offset+(row+1)*bounds.width,
                    composed.begin()+(first+row)*N+bounds.x);
      }
      assert(composed==expected);
    }
  }
}

void checkInvalid(bool wireOrder){
  instrument::Renderer renderer(wireOrder),uninitialized(wireOrder);
  assert(renderer.begin());
  const int invalid[][2]={{-1,1},{N,1},{N+1,1},{0,0},{0,-1},{0,N+1},
                         {N-1,2},{INT_MAX,1},{0,INT_MAX},{INT_MIN,INT_MAX}};
  const uint32_t stages[]={instrument::renderStages[0],instrument::renderStages[1],
                           instrument::renderStages[2],instrument::renderStages[3]};
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const float infinity=std::numeric_limits<float>::infinity();
  const Pose bad[]={{nan,0},{0,nan},{infinity,0},{-infinity,0},{0,infinity},{0,-infinity}};
  const int valid[][2]={{0,1},{465,1},{27,94},{232,1},{0,N}};
  for(bool compact:{false,true}){
    std::vector<uint16_t> untouched(8,Guard);
    for(const auto& range:invalid){
      renderer.renderBackgroundBand(untouched.data()+1,0,0,range[0],range[1],compact);
      renderer.renderForegroundBand(untouched.data()+1,0,0,range[0],range[1],compact);
    }
    renderer.renderBackgroundBand(nullptr,0,0,0,78,compact);
    renderer.renderForegroundBand(nullptr,0,0,0,78,compact);
    for(uint16_t pixel:untouched)assert(pixel==Guard);
    for(const auto& range:valid){
      const int first=range[0],rows=range[1];
      const int width=compact?instrument::circleBandBounds(first,rows).width:N;
      const int pixels=rows*width;
      std::vector<uint16_t> frame(pixels+4,Guard);
      auto checkBlack=[&](){
        checkGuards(frame,1,pixels);
        for(int i=1;i<=pixels;++i)assert(frame[i]==0);
      };
      for(Pose pose:bad){
        std::fill(frame.begin(),frame.end(),Guard);
        renderer.renderForegroundBand(frame.data()+1,pose.pitch,pose.roll,first,rows,compact);
        for(uint16_t pixel:frame)assert(pixel==Guard);
        renderer.renderBackgroundBand(frame.data()+1,pose.pitch,pose.roll,first,rows,compact);
        checkBlack();
        renderer.renderForegroundBand(frame.data()+1,pose.pitch,pose.roll,first,rows,compact);
        checkBlack();
      }
      std::fill(frame.begin(),frame.end(),Guard);
      uninitialized.renderForegroundBand(frame.data()+1,0,0,first,rows,compact);
      for(uint16_t pixel:frame)assert(pixel==Guard);
      uninitialized.renderBackgroundBand(frame.data()+1,0,0,first,rows,compact);
      checkBlack();
      uninitialized.renderForegroundBand(frame.data()+1,0,0,first,rows,compact);
      checkBlack();
    }
  }
  for(int stage=0;stage<4;++stage)assert(instrument::renderStages[stage]==stages[stage]);
}

void checkInterleaved(bool wireOrder){
  instrument::Renderer renderer(wireOrder);
  renderer_circle_reference::Renderer reference(wireOrder);
  assert(renderer.begin() && reference.begin());
  const Pose poses[]={{31,71},{-22,-103},{17,35},{31,71}};
  const int firsts[]={157,0,348,37},rows[]={156,94,118,78};
  std::array<std::vector<uint16_t>,4> bands;
  std::vector<uint16_t> expected(Pixels),packed(Pixels);
  for(int i=0;i<4;++i){
    bands[i].assign(rows[i]*instrument::circleBandBounds(firsts[i],rows[i]).width+4,Guard);
    renderer.renderBackgroundBand(bands[i].data()+1,poses[i].pitch,poses[i].roll,firsts[i],rows[i],true);
  }
  for(int i:{2,0,1,3}){
    const Pose pose=poses[i];
    renderer.renderForegroundBand(bands[i].data()+1,pose.pitch,pose.roll,firsts[i],rows[i],true);
    reference.render(expected.data(),pose.pitch,pose.roll);
    packReference(expected,packed,firsts[i],rows[i]);
    const int pixels=rows[i]*instrument::circleBandBounds(firsts[i],rows[i]).width;
    assert(std::equal(bands[i].begin()+1,bands[i].begin()+1+pixels,packed.begin()));
    checkGuards(bands[i],1,pixels);
  }
}

void checkOverlays(bool wireOrder){
  instrument::Renderer renderer(wireOrder);
  renderer_circle_reference::Renderer reference(wireOrder);
  instrument::FpsOverlay overlay(wireOrder);
  assert(renderer.begin() && reference.begin());
  const int heights[]={48,60,78,94,118,156,1,N};
  const Pose poses[]={{0,0},{-90,-180},{90,180},{-89.999f,89.999f},
    {89.999f,-89.999f},{-17.375f,31.125f},{3.14159f,-127.875f},
    {42.625f,716.75f},{-103.75f,-923.125f}};
  std::vector<uint16_t> expected(Pixels),composed(Pixels);
  int cases=0;
  for(Pose pose:poses)for(int height:heights){
    reference.render(expected.data(),pose.pitch,pose.roll);
    overlay.update(cases%5==0?-1:cases*.37f);
    overlay.blit(expected.data(),N);
    overlay.blitDemoBand(expected.data(),N,0,N);
    std::fill(composed.begin(),composed.end(),0);
    for(int first=0;first<N;first+=height){
      const int rows=std::min(height,N-first),offset=1+(cases&1);
      const instrument::CircleBand bounds=instrument::circleBandBounds(first,rows);
      const int pixels=rows*bounds.width;
      std::vector<uint16_t> band(pixels+4,Guard);
      renderer.renderBackgroundBand(band.data()+offset,pose.pitch,pose.roll,first,rows,true);
      renderer.renderForegroundBand(band.data()+offset,pose.pitch,pose.roll,first,rows,true);
      overlay.blitBand(band.data()+offset,bounds.width,first,rows,bounds.x);
      overlay.blitDemoBand(band.data()+offset,bounds.width,first,rows,bounds.x);
      checkGuards(band,offset,pixels);
      for(int row=0;row<rows;++row)
        std::copy(band.begin()+offset+row*bounds.width,band.begin()+offset+(row+1)*bounds.width,
                  composed.begin()+(first+row)*N+bounds.x);
    }
    assert(composed==expected);
    ++cases;
  }
  std::printf("Direct compact FPS/DEMO: %d %s frames reconstruct exactly at every 48/60/78/94/118/156/1/full-row height PASS\n",cases,wireOrder?"wire":"native");
}

void checkConcurrent(bool wireOrder){
  instrument::Renderer renderer(wireOrder);
  renderer_circle_reference::Renderer reference(wireOrder);
  assert(renderer.begin() && reference.begin());
  struct Job {Pose pose;int first,rows;bool compact;};
  std::vector<Job> jobs;
  for(int i=0;i<32;++i){
    const int height=(i%2)?94:156;
    for(int first=0;first<N;first+=height)
      jobs.push_back({{float(i*7-110),float(i*53-780)},first,std::min(height,N-first),i%3!=0});
  }
  std::array<std::vector<uint16_t>,3> buffers;
  for(auto& buffer:buffers)buffer.resize(156*N+4);
  std::array<bool,3> ready={{false,false,false}};
  std::mutex mutex;
  std::condition_variable changed;
  std::thread producer([&](){
    for(size_t i=0;i<jobs.size();++i){
      const size_t slot=i%buffers.size();
      {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock,[&](){return !ready[slot];});
      }
      const Job job=jobs[i];
      std::fill(buffers[slot].begin(),buffers[slot].end(),Guard);
      renderer.renderBackgroundBand(buffers[slot].data()+1,job.pose.pitch,job.pose.roll,job.first,job.rows,job.compact);
      {
        std::lock_guard<std::mutex> lock(mutex);
        ready[slot]=true;
      }
      changed.notify_one();
    }
  });
  std::vector<uint16_t> expected(156*N);
  for(size_t i=0;i<jobs.size();++i){
    const size_t slot=i%buffers.size();
    {
      std::unique_lock<std::mutex> lock(mutex);
      changed.wait(lock,[&](){return ready[slot];});
    }
    const Job job=jobs[i];
    renderer.renderForegroundBand(buffers[slot].data()+1,job.pose.pitch,job.pose.roll,job.first,job.rows,job.compact);
    reference.renderBand(expected.data(),job.pose.pitch,job.pose.roll,job.first,job.rows);
    const instrument::CircleBand bounds=instrument::circleBandBounds(job.first,job.rows);
    if(job.compact)assert(instrument::packCircleBandInPlace(expected.data(),job.rows,bounds));
    const int pixels=job.rows*(job.compact?bounds.width:N);
    assert(std::equal(buffers[slot].begin()+1,buffers[slot].begin()+1+pixels,expected.begin()));
    checkGuards(buffers[slot],1,pixels);
    {
      std::lock_guard<std::mutex> lock(mutex);
      ready[slot]=false;
    }
    changed.notify_one();
  }
  producer.join();
}
}

int main(){
  // Native panel rendering must carry the fixed aircraft, lighting and
  // attitude markings together, even though the transport stays top to bottom.
  {
    using namespace instrument;
    Renderer native;
    assert(native.begin());
    std::vector<uint16_t> logical(Pixels),expected(Pixels),actual(Pixels);
    for(auto side:{UsbSide::Top,UsbSide::Right,UsbSide::Bottom,UsbSide::Left}){
      assert(native.setInstallation(UsbSide::Left));native.render(logical.data(),14,31);
      copyPanelPixels(expected.data(),logical.data(),N,N,side,0,Pixels);
      assert(native.setInstallation(side));native.render(actual.data(),14,31);
      const auto window=panelRect({0,60,466,60},side);
      std::vector<uint16_t> clipped(window.width*window.height+2,Guard);
      native.renderBackgroundWindow(clipped.data()+1,14,31,window);
      native.renderForegroundWindow(clipped.data()+1,14,31,window);
      assert(clipped.front()==Guard&&clipped.back()==Guard);
      for(int y=0;y<window.height;++y)
        assert(std::equal(clipped.data()+1+y*window.width,clipped.data()+1+(y+1)*window.width,
                          actual.data()+(window.y+y)*N+window.x));
      unsigned changed=0;
      for(int i=0;i<Pixels;++i){
        if(actual[i]==expected[i])continue;
        ++changed;
        for(int shift:{0,5,11}){const int mask=shift==5?63:31;
          assert(std::abs(int((actual[i]>>shift)&mask)-int((expected[i]>>shift)&mask))<=1);}
      }
      assert(changed<80);
      // Compare repeated partial updates with a complete image, including an
      // annulus whose horizon changes sides after the roll crosses ninety.
      std::vector<uint16_t> restored=actual;
      Pose previous{14,31};
      for(Pose pose:{Pose{14.5f,33},Pose{0,0},Pose{-1,-10},Pose{45,85},Pose{89.9f,95},
                     Pose{-90,152},Pose{-36,-126},Pose{28,-41},Pose{1,1},Pose{0,0}}){
        RoundDamage damage(previous.pitch,previous.roll,pose.pitch,pose.roll,side);
        int pixelsWritten=0;
        for(int y=0;y<N;y+=28){
          const int rows=std::min(28,N-y);
          const auto band=circleBandBounds(y,rows);
          const auto r=damage.window({band.x,y,band.width,rows});
          assert((r.x|r.width)%2==0);
          if(!r.width)continue;
          pixelsWritten+=r.width*r.height;
          std::vector<uint16_t> pixels(r.width*r.height);
          native.renderBackgroundWindow(pixels.data(),pose.pitch,pose.roll,r);
          native.renderForegroundWindow(pixels.data(),pose.pitch,pose.roll,r);
          for(int i=0;i<r.height;++i)
            std::copy_n(pixels.data()+i*r.width,r.width,restored.data()+(r.y+i)*N+r.x);
        }
        native.render(actual.data(),pose.pitch,pose.roll);
        assert(restored==actual);
        if(pose.pitch==0&&previous.pitch==1)assert(pixelsWritten<110000);
        previous=pose;
      }
    }
  }
  static constexpr int firstStableRow=instrument::Renderer::StableBottomRow;
  const int stableRows=N-firstStableRow;
  instrument::Renderer stableRenderer;
  assert(stableRenderer.begin());
  std::vector<uint16_t> expectedBottom(stableRows*N),actualBottom(stableRows*N);
  stableRenderer.renderBand(expectedBottom.data(),0,0,firstStableRow,stableRows);
  for(float pitch:{-45.f,-32.f,0.f,32.f,45.f})for(float roll:{-44.f,-20.f,0.f,20.f,44.f}){
    assert(instrument::Renderer::hasStableBottom(pitch,roll));
    stableRenderer.renderBand(actualBottom.data(),pitch,roll,firstStableRow,stableRows);
    assert(actualBottom==expectedBottom);
  }
  assert(!instrument::Renderer::hasStableBottom(0,90));
  assert(!instrument::Renderer::hasStableBottom(-90,0));
  assert(!instrument::Renderer::hasStableBottom(std::numeric_limits<float>::quiet_NaN(),0));
  const float pitches[]={-100,-90,-89.999f,-60,-1,0,1,60,89.999f,90,100};
  const float rolls[]={-180,-90,-89.999f,-.001f,0,.001f,89.999f,90,180};
  std::vector<Pose> poses;
  for(float pitch:pitches)for(float roll:rolls)poses.push_back({pitch,roll});
  uint32_t state=0xc181a47b;
  auto random=[&](){state=state*1664525+1013904223;return state*(1./4294967296.);};
  for(int i=0;i<100;++i)poses.push_back({float(random()*220-110),float(random()*2160-1080)});
  const float maximum=std::numeric_limits<float>::max();
  poses.push_back({maximum,maximum});poses.push_back({-maximum,-maximum});
  poses.push_back({0,maximum});poses.push_back({-0.f,-0.f});
  for(int i=0;i<4;++i)instrument::renderStages[i]=17+i;
  for(bool wireOrder:{false,true}){
    compareBands(wireOrder,poses);
    checkInvalid(wireOrder);
    checkInterleaved(wireOrder);
    checkOverlays(wireOrder);
    checkConcurrent(wireOrder);
  }
  std::printf("Direct compact circle: %zu poses in native/wire order exactly match full-width + pack and frozen fixture for every 48/60/78/94/118/156/1/full-row height; background parity, reconstruction, interleaving, threaded ownership, guards and invalid inputs PASS\n",poses.size());
}
