#include "renderer.h"
#undef INSTRUMENT_RENDERER_H
#define instrument renderer_stages_reference
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

// Link the frozen background_pose_reference renderer with
// -Dinstrument=renderer_stages_reference, independently of the current wrapper.
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

void compareBands(bool wireOrder,const std::vector<Pose>& poses){
  instrument::Renderer renderer(wireOrder);
  renderer_stages_reference::Renderer reference(wireOrder);
  assert(renderer.begin() && reference.begin());
  const int heights[]={48,60,78,94,118,156,1,N};
  std::vector<uint16_t> expected(Pixels),full(Pixels),composed(Pixels);
  std::vector<uint16_t> band(Pixels+4),ahead(Pixels+4);
  for(size_t i=0;i<poses.size();++i){
    const Pose pose=poses[i],next=poses[(i+1)%poses.size()];
    reference.render(expected.data(),pose.pitch,pose.roll);
    renderer.render(full.data(),pose.pitch,pose.roll);
    assert(full==expected);
    const int height=heights[i%8],offset=1+(i&1);
    for(int first=0;first<N;first+=height){
      const int rows=std::min(height,N-first);
      std::fill(band.begin(),band.end(),Guard);
      renderer.renderBackgroundBand(band.data()+offset,pose.pitch,pose.roll,first,rows);
      checkGuards(band,offset,rows*N);
      // A future pose can prepare any band without invalidating foreground's
      // cached pose, even while this band's background waits to be decorated.
#if defined(INSTRUMENT_TEST_POSE_CACHE)
      const size_t builds=instrument::backgroundPoseBuilds;
#endif
      const uint32_t stages[]={instrument::renderStages[0],instrument::renderStages[1],
                               instrument::renderStages[2],instrument::renderStages[3]};
      renderer.renderBackgroundBand(ahead.data()+offset,next.pitch,next.roll,N-rows,rows);
      for(int stage=0;stage<4;++stage)assert(instrument::renderStages[stage]==stages[stage]);
      renderer.renderForegroundBand(band.data()+offset,pose.pitch,pose.roll,first,rows);
      assert(instrument::renderStages[0]==stages[0]);
#if defined(INSTRUMENT_TEST_POSE_CACHE)
      assert(instrument::backgroundPoseBuilds==builds);
#endif
      checkGuards(band,offset,rows*N);
      assert(std::equal(band.begin()+offset,band.begin()+offset+rows*N,expected.begin()+first*N));
      std::copy(band.begin()+offset,band.begin()+offset+rows*N,composed.begin()+first*N);
    }
    assert(composed==expected);
  }
}

void checkInvalid(bool wireOrder){
  instrument::Renderer renderer(wireOrder),uninitialized(wireOrder);
  assert(renderer.begin());
  std::vector<uint16_t> frame(Pixels+4,Guard);
  const int invalid[][2]={{-1,1},{N,1},{N+1,1},{0,0},{0,-1},{0,N+1},
                         {N-1,2},{INT_MAX,1},{0,INT_MAX},{INT_MIN,INT_MAX}};
  const uint32_t stages[]={instrument::renderStages[0],instrument::renderStages[1],
                           instrument::renderStages[2],instrument::renderStages[3]};
  for(const auto& range:invalid){
    renderer.renderBackgroundBand(frame.data()+1,0,0,range[0],range[1]);
    renderer.renderForegroundBand(frame.data()+1,0,0,range[0],range[1]);
    renderer.renderBand(frame.data()+1,0,0,range[0],range[1]);
  }
  renderer.renderBackgroundBand(nullptr,0,0,0,78);
  renderer.renderForegroundBand(nullptr,0,0,0,78);
  renderer.renderBand(nullptr,0,0,0,78);
  for(uint16_t pixel:frame)assert(pixel==Guard);
  const float nan=std::numeric_limits<float>::quiet_NaN();
  const float infinity=std::numeric_limits<float>::infinity();
  const Pose bad[]={{nan,0},{0,nan},{infinity,0},{-infinity,0},{0,infinity},{0,-infinity}};
  const int first=27,rows=94;
  auto checkBlack=[&](){
    checkGuards(frame,1,rows*N);
    for(int i=1;i<=rows*N;++i)assert(frame[i]==0);
  };
  for(Pose pose:bad){
    std::fill(frame.begin(),frame.end(),Guard);
    renderer.renderForegroundBand(frame.data()+1,pose.pitch,pose.roll,first,rows);
    for(uint16_t pixel:frame)assert(pixel==Guard);
    renderer.renderBackgroundBand(frame.data()+1,pose.pitch,pose.roll,first,rows);
    checkBlack();
    renderer.renderForegroundBand(frame.data()+1,pose.pitch,pose.roll,first,rows);
    checkBlack();
    std::fill(frame.begin(),frame.end(),Guard);
    renderer.renderBand(frame.data()+1,pose.pitch,pose.roll,first,rows);
    checkBlack();
  }
  std::fill(frame.begin(),frame.end(),Guard);
  uninitialized.renderForegroundBand(frame.data()+1,0,0,first,rows);
  for(uint16_t pixel:frame)assert(pixel==Guard);
  uninitialized.renderBackgroundBand(frame.data()+1,0,0,first,rows);
  checkBlack();
  uninitialized.renderForegroundBand(frame.data()+1,0,0,first,rows);
  checkBlack();
  std::fill(frame.begin(),frame.end(),Guard);
  uninitialized.renderBand(frame.data()+1,0,0,first,rows);
  checkBlack();
  for(int stage=0;stage<4;++stage)assert(instrument::renderStages[stage]==stages[stage]);
}

void checkWrapperTiming(){
#if defined(ARDUINO_ARCH_ESP32)
  instrument::Renderer renderer;
  assert(renderer.begin());
  std::vector<uint16_t> frame(Pixels);
  for(int i=0;i<4;++i)instrument::renderStages[i]=UINT32_MAX;
  renderer.renderBand(frame.data(),17,35,0,N);
  for(int i=0;i<4;++i)assert(instrument::renderStages[i]!=UINT32_MAX);
#if defined(INSTRUMENT_TEST_STAGE_CLOCK)
  // The host timing shim advances one tick per read: each stage owns exactly
  // its start/end pair, with no nested background timing or foreground charge.
  for(int i=0;i<4;++i)assert(instrument::renderStages[i]==1);
#endif
#endif
}

void checkInterleaved(bool wireOrder){
  instrument::Renderer renderer(wireOrder);
  renderer_stages_reference::Renderer reference(wireOrder);
  assert(renderer.begin() && reference.begin());
  const Pose poses[]={{31,71},{-22,-103},{17,35},{31,71}};
  const int firsts[]={157,0,348,37},rows[]={156,94,118,78};
  std::array<std::vector<uint16_t>,4> bands;
  std::vector<uint16_t> expected(Pixels);
  for(int i=0;i<4;++i){
    bands[i].assign(rows[i]*N+4,Guard);
    renderer.renderBackgroundBand(bands[i].data()+1,poses[i].pitch,poses[i].roll,firsts[i],rows[i]);
  }
  // Foreground visits different poses and arbitrary rows in a different order.
  for(int i:{2,0,1,3}){
    const Pose pose=poses[i];
    renderer.renderForegroundBand(bands[i].data()+1,pose.pitch,pose.roll,firsts[i],rows[i]);
    reference.render(expected.data(),pose.pitch,pose.roll);
    assert(std::equal(bands[i].begin()+1,bands[i].begin()+1+rows[i]*N,expected.begin()+firsts[i]*N));
    checkGuards(bands[i],1,rows[i]*N);
  }
}

void checkConcurrent(bool wireOrder){
  instrument::Renderer renderer(wireOrder);
  renderer_stages_reference::Renderer reference(wireOrder);
  assert(renderer.begin() && reference.begin());
  struct Job {Pose pose;int first,rows;};
  std::vector<Job> jobs;
  for(int i=0;i<32;++i){
    const int height=(i%2)?94:156;
    for(int first=0;first<N;first+=height)
      jobs.push_back({{float(i*7-110),float(i*53-780)},first,std::min(height,N-first)});
  }
  std::array<std::vector<uint16_t>,3> buffers;
  for(auto& buffer:buffers)buffer.resize(156*N+4);
  std::array<bool,3> ready={{false,false,false}};
  std::mutex mutex;
  std::condition_variable changed;
  // Only buffer ownership crosses the mutex. Background must not touch the
  // mutable pose cache or foreground timing while both threads share Renderer.
  std::thread producer([&](){
    for(size_t i=0;i<jobs.size();++i){
      const size_t slot=i%buffers.size();
      {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock,[&](){return !ready[slot];});
      }
      const Job job=jobs[i];
      std::fill(buffers[slot].begin(),buffers[slot].end(),Guard);
      renderer.renderBackgroundBand(buffers[slot].data()+1,job.pose.pitch,job.pose.roll,job.first,job.rows);
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
    renderer.renderForegroundBand(buffers[slot].data()+1,job.pose.pitch,job.pose.roll,job.first,job.rows);
    reference.renderBand(expected.data(),job.pose.pitch,job.pose.roll,job.first,job.rows);
    assert(std::equal(buffers[slot].begin()+1,buffers[slot].begin()+1+job.rows*N,expected.begin()));
    checkGuards(buffers[slot],1,job.rows*N);
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
  const float pitches[]={-100,-90,-89.999f,-60,-1,0,1,60,89.999f,90,100};
  const float rolls[]={-180,-90,-89.999f,-.001f,0,.001f,89.999f,90,180};
  std::vector<Pose> poses;
  for(float pitch:pitches)for(float roll:rolls)poses.push_back({pitch,roll});
  uint32_t state=0xc181a47b;
  auto random=[&](){state=state*1664525+1013904223;return state*(1./4294967296.);};
  for(int i=0;i<100;++i)poses.push_back({float(random()*220-110),float(random()*2160-1080)});
  for(int i=0;i<4;++i)instrument::renderStages[i]=17+i;
  for(bool wireOrder:{false,true}){
    compareBands(wireOrder,poses);
    checkInvalid(wireOrder);
    checkInterleaved(wireOrder);
    checkConcurrent(wireOrder);
  }
  checkWrapperTiming();
  std::printf("Renderer stages: %zu poses in native/wire order exactly match frozen reference and wrapper; 48/60/78/94/118/156/1/full rows, interleaving, threaded ownership, guards and invalid inputs PASS\n",poses.size());
}
