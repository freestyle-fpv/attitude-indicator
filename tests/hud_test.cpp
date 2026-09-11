#include "hud_renderer.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>
using namespace instrument;
int main(){
  // Unchanged leading digits stay centered while changed digits roll together.
  for(bool altitude : {false,true}){
    HudRenderer counter;assert(counter.begin());HudInput input;input.speedKmh=100;input.altitudeM=1000;
    const AirspeedRect window=altitude?AirspeedRect{394,218,62,32}:AirspeedRect{9,218,62,32};
    std::vector<uint16_t> before(window.width*window.height),after(before.size()),settled(before.size());
    auto draw=[&](uint32_t now,std::vector<uint16_t>& pixels){input.nowMs=now;counter.prepareFrame(0,input);counter.renderPrepared(pixels.data(),0,window);};
    draw(0,before);if(altitude)input.altitudeM+=.5f;else input.speedKmh+=.5f;
    draw(10,after);assert(before!=after);
    unsigned leadingChanges=0;for(int y=0;y<32;++y)for(int x=5;x<20;++x)leadingChanges+=before[y*window.width+x]!=after[y*window.width+x];assert(leadingChanges==0);
    settled=after;draw(10000,after);assert(settled==after);
    if(altitude)input.altitudeM+=.5f;else input.speedKmh+=.5f;draw(10001,after);
    HudRenderer fresh;assert(fresh.begin());fresh.prepareFrame(0,input);fresh.renderPrepared(settled.data(),0,window);assert(after==settled);

  }
  HudRenderer r(false),wire(true);assert(r.begin()&&wire.begin());
  HudInput p;std::vector<uint16_t> full(466*466),w(full.size()),rebuilt(full.size());
  r.render(full.data(),p);wire.render(w.data(),p);
  unsigned lit=0;for(size_t i=0;i<full.size();++i){assert(w[i]==uint16_t((full[i]<<8)|(full[i]>>8)));if(full[i])++lit;}
  assert(lit>1500&&lit<30000);assert(full[0]==0&&full.back()==0);
  // 0 and 10 degrees have identical long-line geometry; the intervening
  // 5-degree mark is shorter. All rows have a continuous center and labels.
  for(int x=153;x<=313;++x){assert(full[174*466+x]!=0);assert(full[294*466+x]!=0);}
  assert(full[234*466+182]==0&&full[234*466+187]!=0);
  assert(full[177*466+153]!=0&&full[177*466+313]!=0);
  unsigned shortLabelPixels=0;for(int y=223;y<245;++y)for(int x=167;x<183;++x)shortLabelPixels+=full[y*466+x]!=0;
  assert(shortLabelPixels>20);
  // Outer and inner center rings over the continuous pitch line.
  assert(full[234*466+233]!=0);assert(full[234*466+241]!=0);assert(full[234*466+247]!=0);
  rebuilt=full;
  for(int step=0;step<20;++step){
    p.nowMs+=80;p.heading=step%2?359:1;p.roll=-60+6*step;p.pitch=-25+step*2;
    p.speedKmh=step%2?200:0;p.altitudeM=step%2?3000:0;p.climb=-15+step;
    assert(r.prepareFrame(0,p));r.renderPrepared(full.data(),0,{0,0,466,466});
    const auto damage=HudRenderer::damage();int bottom=0;
    for(int i=0;i<damage.count;++i){auto a=damage.regions[i];assert(a.y>=bottom);bottom=a.y+a.height;
      assert((a.x|a.y|a.width|a.height)%2==0);
      std::vector<uint16_t> patch(a.width*a.height+2,0xABCD);r.renderPrepared(patch.data()+1,0,a);
      assert(patch.front()==0xABCD&&patch.back()==0xABCD);
      for(int y=0;y<a.height;++y)std::memcpy(&rebuilt[(a.y+y)*466+a.x],patch.data()+1+y*a.width,a.width*2);
    }
    assert(full==rebuilt);
  }
  // A tilted ladder changing pitch must never be transmitted down a strip
  // and then back up another strip: a live panel can scan between those writes.
  HudRenderer scan;assert(scan.begin());HudInput tilted;tilted.roll=35;
  scan.prepareFrame(0,tilted);
  for(int step=1;step<=12;++step){
    tilted.pitch=step*.37f;scan.prepareFrame(0,tilted);
    const auto spans=scan.damagePrepared(0);int writtenBottom=0;
    for(int i=0;i<spans.count;++i){const auto a=spans.regions[i];
      if(a.y<writtenBottom){std::fprintf(stderr,"scan backtracks at pitch %.2f: row %d after %d\n",tilted.pitch,a.y,writtenBottom);assert(false);}
      writtenBottom=a.y+a.height;
    }
  }
  // Identical poses only need the shared FPS overlay, not the full HUD bands.
  HudRenderer dirty;assert(dirty.begin());HudInput motion;
  dirty.render(rebuilt.data(),motion);dirty.prepareFrame(1,motion);
  auto quiet=dirty.damagePrepared(1);int quietArea=0;
  for(int i=0;i<quiet.count;++i)quietArea+=quiet.regions[i].width*quiet.regions[i].height;
  assert(quietArea<8000);
  // Heading label insertion/removal must not dirty unchanged side instruments.
  HudRenderer headingOnly;assert(headingOnly.begin());HudInput headingInput;
  headingOnly.prepareFrame(0,headingInput);
  for(int degree=0;degree<360;degree+=3){headingInput.heading=degree;headingOnly.prepareFrame(0,headingInput);
    const auto headDamage=headingOnly.damagePrepared(0);
    for(int i=0;i<headDamage.count;++i){const auto a=headDamage.regions[i];assert(a.y+a.height<=112||a.y>=416);}
  }
  unsigned random=12345;size_t damageTotal=0;
  for(int frame=0;frame<350;++frame){
    random=random*1664525u+1013904223u;
    motion.nowMs+=25;motion.heading=359+frame*.21f;motion.pitch=std::sin(frame*.035f)*30;
    motion.roll=std::sin(frame*.023f)*178;motion.speedKmh=100+std::sin(frame*.018f)*99;
    motion.altitudeM=1500+std::sin(frame*.031f)*1499;motion.climb=std::sin(frame*.01f)*10;
    motion.attitudeValid=frame%47!=0;
    if(frame%39==0){motion.pitch=int(random%180)-90;motion.roll=int(random%720)-360;}
    const unsigned slot=frame%3;dirty.prepareFrame(slot,motion);dirty.renderPrepared(full.data(),slot,{0,0,466,466});
    const auto changes=dirty.damagePrepared(slot);assert(changes.count>0&&changes.count<=AltimeterDamage::Capacity);
    std::vector<bool> covered(full.size(),false);int lastY=-1,lastX=-1,writtenBottom=0;
    for(int i=0;i<changes.count;++i){const auto a=changes.regions[i];
      assert(a.x>=0&&a.y>=0&&a.x+a.width<=466&&a.y+a.height<=466);
      assert(a.width>0&&a.height>0&&((a.x|a.y|a.width|a.height)&1)==0);
      assert(a.y>lastY||(a.y==lastY&&a.x>lastX));lastY=a.y;lastX=a.x;
      assert(a.y>=writtenBottom);writtenBottom=a.y+a.height;
      std::vector<uint16_t> patch(a.width*a.height);dirty.renderPrepared(patch.data(),slot,a);
      damageTotal+=patch.size();
      for(int y=0;y<a.height;++y)for(int x=0;x<a.width;++x){const int at=(a.y+y)*466+a.x+x;
        assert(!covered[at]);covered[at]=true;rebuilt[at]=patch[y*a.width+x];}
    }
    for(int y=432;y<454;++y)for(int x=181;x<285;++x)assert(covered[y*466+x]);
    if(full!=rebuilt){std::fprintf(stderr,"dirty reconstruction failed frame %d\n",frame);assert(false);}
  }
  std::printf("dynamic damage average %zu pixels\n",damageTotal/350);
  // Beyond the bank scale, the pitch ladder must still follow the real roll.
  p.roll=60;p.pitch=12;p.nowMs+=1000;r.render(full.data(),p);
  p.roll=90;r.render(w.data(),p);assert(full!=w);
  // A complete pitch label near the top boundary stays bright. Once it no
  // longer fits, remove the whole label instead of dimming/clipping digits.
  HudInput edge;edge.pitch=10-109.f/12;edge.roll=0;r.render(full.data(),edge);
  unsigned edgePeak=0;
  for(int y=114;y<149;++y)for(int x=126;x<151;++x)edgePeak=std::max(edgePeak,unsigned((full[y*466+x]>>5)&63));
  assert(edgePeak>=62);
  edge.pitch=10-114.f/12;r.render(full.data(),edge);
  for(int y=114;y<149;++y)for(int x=126;x<151;++x)assert(full[y*466+x]==0);
  edge.pitch=-5;r.render(full.data(),edge);
  assert(full[294*466+233]!=0&&full[294*466+240]==0&&full[294*466+245]!=0);
  assert(full[291*466+153]!=0&&full[291*466+313]!=0);
  // HUD regions have exactly one worker owner, including the bottom scale.
  // Reconstruct a frame with the actual two-worker assignment.
  HudInput parallel;assert(r.prepareFrame(2,parallel));r.renderPrepared(full.data(),2,{0,0,466,466});
  std::fill(rebuilt.begin(),rebuilt.end(),0);const auto regions=HudRenderer::damage();
  auto worker=[&](bool background){for(int i=0;i<regions.count;++i){
    if(HudRenderer::backgroundRegion(i)!=background)continue;const auto rect=regions.regions[i];
    std::vector<uint16_t> patch(rect.width*rect.height);r.renderPrepared(patch.data(),2,rect);
    for(int y=0;y<rect.height;++y)std::memcpy(&rebuilt[(rect.y+y)*466+rect.x],patch.data()+y*rect.width,rect.width*2);
  }};
  std::thread background([&]{worker(true);});worker(false);background.join();assert(full==rebuilt);
  // A scale label fades near the readout cutout instead of vanishing at full brightness.
  const float labelRadius=(104.f-3*19.203125f*22/32/2)/std::sin(3.14159265358979323846f/3)-23;
  const float scale=labelRadius*3.14159265358979323846f/18/5;
  auto labelPeak=[&](float y){HudRenderer fade;assert(fade.begin());HudInput input;input.speedKmh=80+(y-234)/scale;
    fade.render(full.data(),input);unsigned peak=0;
    for(int yy=int(y)-8;yy<=int(y)+8;++yy)for(int x=30;x<65;++x)peak=std::max(peak,unsigned((full[yy*466+x]>>5)&63));return peak;
  };
  assert(labelPeak(184)>50);assert(labelPeak(199)>0&&labelPeak(199)<labelPeak(184)/2);
  assert(labelPeak(202)==0);assert(labelPeak(139)<labelPeak(157));
  // Independent immutable slots can render simultaneously.
  p.nowMs+=100;r.prepareFrame(0,p);p.roll=-44;p.heading=279;r.prepareFrame(1,p);
  std::vector<uint16_t> a(full.size()),b(full.size()),again(full.size());
  std::thread t([&]{r.renderPrepared(a.data(),0,{0,0,466,466});});r.renderPrepared(b.data(),1,{0,0,466,466});t.join();
  r.renderPrepared(again.data(),0,{0,0,466,466});assert(a==again&&a!=b);
  p.roll=std::numeric_limits<float>::max();p.attitudeValid=true;r.render(full.data(),p);
  p.pitch=p.roll=p.heading=p.speedKmh=p.altitudeM=p.climb=std::numeric_limits<float>::quiet_NaN();p.attitudeValid=false;
  r.render(full.data(),p);assert(full[0]==0);assert(!r.prepareFrame(3,p));
  std::puts("PASS HUD guards, wire order, double ring, scan-order reconstruction, wrap/extremes, immutable slots and invalid inputs");
}
