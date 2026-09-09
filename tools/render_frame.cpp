#include "renderer.h"
#include "fps_overlay.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>
static void u16(FILE* f,uint16_t v){std::fputc(v&255,f);std::fputc(v>>8,f);}
static void u32(FILE* f,uint32_t v){u16(f,v&65535);u16(f,v>>16);}
static bool number(const char* s,float& v){char* end=nullptr;errno=0;v=std::strtof(s,&end);return end!=s && !*end && !errno && std::isfinite(v);}
int main(int argc,char** argv){
  float pitch,roll,fps=-1;
  if((argc!=4&&argc!=5)||!number(argv[1],pitch)||!number(argv[2],roll)||(argc==5&&!number(argv[4],fps))){std::fprintf(stderr,"Usage: render_frame PITCH ROLL OUTPUT.bmp [FPS]\n");return 2;}
  instrument::Renderer renderer;
  if(!renderer.begin()){std::fprintf(stderr,"Renderer allocation failed\n");return 1;}
  std::vector<uint16_t> frame(466*466);renderer.render(frame.data(),pitch,roll);
  if(argc==5){instrument::FpsOverlay overlay;overlay.update(fps);overlay.blit(frame.data(),466);}
  FILE* f=std::fopen(argv[3],"wb");if(!f){std::perror(argv[3]);return 1;}
  const int stride=(466*3+3)&~3;
  std::fputc('B',f);std::fputc('M',f);u32(f,54+stride*466);u32(f,0);u32(f,54);
  u32(f,40);u32(f,466);u32(f,466);u16(f,1);u16(f,24);u32(f,0);u32(f,stride*466);u32(f,2835);u32(f,2835);u32(f,0);u32(f,0);
  for(int y=465;y>=0;--y){for(int x=0;x<466;++x){uint16_t c=frame[y*466+x];std::fputc((c&31)*255/31,f);std::fputc(((c>>5)&63)*255/63,f);std::fputc((c>>11)*255/31,f);}for(int p=466*3;p<stride;++p)std::fputc(0,f);}
  bool failed=std::ferror(f);if(std::fclose(f))failed=true;return failed?1:0;
}
