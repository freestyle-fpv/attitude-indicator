#include "hud_readout.h"
#include <cassert>
#include <cmath>
#include <cstdio>
int main(){
  instrument::HudReadout r;r.update(100);r.update(100.25f);
  assert(r.moving&&r.from==100&&r.to==101&&std::fabs(r.progress()-.25f)<.0001f);
  r.update(100.75f);assert(std::fabs(r.progress()-.75f)<.0001f);
  r.update(100.75f);assert(std::fabs(r.progress()-.75f)<.0001f);
  r.update(100.5f);assert(std::fabs(r.progress()-.5f)<.0001f);
  r.update(129.25f);assert(r.from==129&&r.to==130&&std::fabs(r.progress()-.25f)<.0001f);
  r.update(3000);assert(!r.moving&&r.from==3000&&r.to==3000);
  const int pairs[][3]={{128,129,1},{129,130,2},{199,200,3},{999,1000,4},{109,119,1},{130,129,2},{1000,999,4},{0,1,1}};
  for(const auto& pair:pairs){instrument::HudReadout digits;digits.from=pair[0];digits.to=pair[1];digits.moving=true;
    const auto g=digits.glyphs(4);char from[8],to[8];std::snprintf(from,sizeof(from),"%4d",pair[0]);std::snprintf(to,sizeof(to),"%4d",pair[1]);
    int changed=0;for(int i=0;i<4;++i){if(from[i]!=to[i]){++changed;assert(g.fixed[i]==' '&&g.outgoing[i]==from[i]&&g.incoming[i]==to[i]);}
      else assert(g.fixed[i]==to[i]&&g.outgoing[i]==' '&&g.incoming[i]==' ');}
    assert(changed==pair[2]);
  }
  std::puts("PASS selective 1/2/3/4-digit rolls, carries/borrows, unchanged digits and continuous axis-driven position");
}
