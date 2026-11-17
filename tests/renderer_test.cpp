#include "renderer.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* message) {
  if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
static bool sky(uint16_t c) { return (c & 31) * 8 > ((c >> 11) & 31) * 8 + 25; }
static bool ground(uint16_t c) { return ((c >> 11) & 31) > (c & 31) + 4; }
static bool white(uint16_t c) { return (c >> 11) >= 28 && ((c >> 5) & 63) >= 56 && (c & 31) >= 25; }
static bool yellow(uint16_t c) { return (c >> 11) >= 27 && ((c >> 5) & 63) >= 45 && (c & 31) < 8; }
int main() {
  instrument::Renderer renderer;
  check(renderer.begin(), "precomputation initializes");
  check(renderer.begin(), "repeated begin is safe");
  const int n = 466;
  std::vector<uint16_t> guarded(n*n+2, 0xA55A);
  uint16_t* frame = guarded.data()+1;
  renderer.render(frame, 0, 0);
  check(guarded.front()==0xA55A && guarded.back()==0xA55A, "frame writes stay in bounds");
  check(frame[0]==0 && frame[n-1]==0 && frame[n*(n-1)]==0 && frame[n*n-1]==0, "corners clipped black");
  check(ground(frame[430*n+232]), "bottom center filled, not cropped");
  check(ground(frame[460*n+232]), "bottom reaches the circular perimeter");
  bool clipped=true;
  for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
    float dx=x-232.5f,dy=y-232.5f;
    if(dx*dx+dy*dy>232.5f*232.5f && frame[y*n+x]!=0)clipped=false;
  }
  check(clipped,"all pixels outside the circular display are black");
  // Sample between the ring and the numbered pitch ladder.
  check(sky(frame[145*n+150]), "level upper hemisphere is sky");
  check(ground(frame[320*n+150]), "level lower hemisphere is ground");
  check(ground(frame[255*n+320]), "level lower-right sample is ground");
  check(white(frame[231*n+80]) && white(frame[234*n+80]) &&
        white(frame[231*n+25]) && white(frame[234*n+25]),
        "inner and outer horizons have four bright rows at level");
  check(!white(frame[217*n+224]), "the unnumbered five-degree pitch mark is omitted");
  std::vector<int> yellowPixels;
  for (int y=222; y<281; ++y) for (int x=108; x<358; ++x)
    // Opaque yellow must remain fixed; translucent edge pixels deliberately
    // change as the moving background and pointer shadow pass underneath.
    if (frame[y*n+x]==0xff42) yellowPixels.push_back(y*n+x);
  check(yellowPixels.size()>500, "aircraft has bright yellow center and wings");
  check(yellow(frame[232*n+232]), "center aircraft dot visible");
  check(white(frame[25*n+238]), "white bank index starts above center");
  check(!white(frame[50*n+133]), "bank index destination initially clear");
  std::vector<uint16_t> levelFrame(frame,frame+n*n);
  renderer.render(frame, 25, 0);
  bool annulusUnchanged=true;
  for(int y=0;y<n;++y)for(int x=0;x<n;++x) {
    float dx=x-232.5f,dy=y-232.5f;
    if(dx*dx+dy*dy>181.f*181.f && frame[y*n+x]!=levelFrame[y*n+x])annulusUnchanged=false;
  }
  check(annulusUnchanged,"outer horizon is independent of pitch");
  check(sky(frame[255*n+320]), "nose-up moves horizon down and increases sky");
  for (size_t i=0;i<yellowPixels.size();++i) {
    if (!yellow(frame[yellowPixels[i]])) {check(false,"yellow aircraft fixed under pitch"); break;}
  }
  renderer.render(frame, 0, 30);
  check(!white(frame[25*n+238]), "white bank index leaves its level position");
  check(white(frame[50*n+133]), "white bank index rotates counterclockwise with positive roll");
  check(yellow(frame[80*n+232]), "yellow bank pointer stays fixed under roll");
  check(sky(frame[240*n+150]), "right bank places sky below center on left");
  check(ground(frame[220*n+320]), "right bank places ground above center on right");
  for (size_t i=0;i<yellowPixels.size();++i) {
    if (!yellow(frame[yellowPixels[i]])) {check(false,"yellow aircraft fixed under roll"); break;}
  }
  renderer.render(frame, 0, 180);
  check(ground(frame[145*n+150]) && sky(frame[320*n+150]), "180 degree roll swaps sky and ground");
  renderer.render(frame, -25, 0);
  check(ground(frame[210*n+300]), "nose-down moves ground above center");
  renderer.render(frame, 10, 179.9f);
  std::vector<uint16_t> beforeWrap(frame,frame+n*n);
  renderer.render(frame, 10, -179.9f);
  double difference=0;
  for(int i=0;i<n*n;++i) {
    uint16_t a=beforeWrap[i], b=frame[i];
    difference+=std::abs(int(a>>11)-int(b>>11))+std::abs(int((a>>5)&63)-int((b>>5)&63))+std::abs(int(a&31)-int(b&31));
  }
  std::printf("Roll wrap mean RGB565 channel difference: %.4f / 125\n",difference/(n*n));
  check(difference/(n*n)<0.5,"roll remains visually continuous across 180 degrees");
  renderer.render(frame, 90, 179.9f);
  check(guarded.front()==0xA55A && guarded.back()==0xA55A, "extreme attitude stays in bounds");
  renderer.render(nullptr, 0, 0);
  std::printf("Renderer: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
  return failures ? 1 : 0;
}
