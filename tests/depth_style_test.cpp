#include "renderer.h"
#include <cmath>
#include <cstdio>
#include <vector>

// These checks catch missing transparency, shadows baked below moving markings,
// and a hard, single-step circular boundary. They do not snapshot the artwork.
int main(){
  instrument::Renderer renderer;
  if(!renderer.begin())return 1;
  const int n=466;
  std::vector<uint16_t> background(n*n),frame(n*n);
  int failures=0;
  auto check=[&](bool ok,const char* message){
    if(!ok){std::printf("FAIL: %s\n",message);++failures;}
  };
  for(float pitch:{-30.f,0.f,30.f}){
    renderer.renderBackgroundBand(background.data(),pitch,0,0,n);
    frame=background;
    renderer.renderForegroundBand(frame.data(),pitch,0,0,n);
    bool clear=true;
    for(int y=391;y<404;++y)for(int x=202;x<217;++x)
      if(frame[y*n+x]!=background[y*n+x])clear=false;
    check(clear,"removed pedestal reveals the moving ball below it");
    check((frame[399*n+232]>>11)>=20 && (frame[399*n+232]&31)<8,
          "yellow support reaches the lower lip instead of ending in the ball");
    check(frame[410*n+232]==background[410*n+232],
          "outer ring occludes the support's lower end");
  }
  std::fill(frame.begin(),frame.end(),0xffff);
  renderer.renderForegroundBand(frame.data(),0,0,0,n);
  check(frame[242*n+145]!=0xffff && frame[242*n+145]!=0,
        "wing casts a translucent shadow over foreground markings");
  check(frame[99*n+232]!=0xffff && frame[99*n+232]!=0,
        "upper pointer casts a translucent downward shadow");
  check(frame[219*n+145]==0xffff,"wing shadow does not stain the upper side");
  renderer.renderBackgroundBand(frame.data(),90,0,0,n);
  // At 12 o'clock the outer edge must span intermediate intensities instead
  // of jumping directly from the blue annulus to a constant dark rim.
  int transitions=0;
  for(int y=54;y<61;++y)
    if(((frame[y*n+232]>>5)&63)!=((frame[(y+1)*n+232]>>5)&63))++transitions;
  check(transitions>=4,"inner ring outer boundary has a gradual pixel transition");
  renderer.renderBackgroundBand(frame.data(),0,15,0,n);
  check(((frame[188*n+399]>>5)&63)<20 && ((frame[277*n+66]>>5)&63)<20,
        "rotating white horizon stays occluded at both dark inner lips");
  renderer.renderBackgroundBand(frame.data(),0,0,0,n);
  check(((frame[232*n+405]>>5)&63)<20,
        "level white horizon cannot create a bright pixel on the dark lip");
  std::printf("Depth style: %s (%d failures)\n",failures?"FAIL":"PASS",failures);
  return failures?1:0;
}
