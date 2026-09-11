#pragma once
#include <cmath>
#include <cstdio>
#include <cstring>
namespace instrument {
// Position-driven rolling: the axis value sets digit positions directly.
// No timers, detents or queued intermediate readings.
struct HudReadout {
  int from=0,to=0;
  float phase=0;
  bool moving=false;
  struct GlyphLayers {char fixed[8]={},outgoing[8]={},incoming[8]={};};
  GlyphLayers glyphs(int digits)const{
    GlyphLayers g;std::snprintf(g.outgoing,sizeof(g.outgoing),"%*d",digits,from);
    std::snprintf(g.incoming,sizeof(g.incoming),"%*d",digits,to);
    std::strcpy(g.fixed,g.incoming);
    for(int i=0;g.incoming[i];++i){
      if(moving&&g.outgoing[i]!=g.incoming[i])g.fixed[i]=' ';
      else g.outgoing[i]=g.incoming[i]=' ';
    }
    return g;
  }
  float progress() const {return phase;}
  void update(float value){
    from=int(std::floor(value));phase=value-from;
    moving=phase>0;to=from+(moving?1:0);
  }
};
}
