#include "altimeter_renderer.h"
#include "fps_overlay.h"
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
static void u16(FILE* f,unsigned v){std::fputc(v&255,f);std::fputc((v>>8)&255,f);}
static void u32(FILE* f,unsigned v){u16(f,v);u16(f,v>>16);}
int main(int argc,char** argv){
  if(argc!=3){std::fprintf(stderr,"Usage: render_altimeter METERS OUTPUT.bmp\n");return 2;}
  char* end=nullptr;errno=0;float altitudeM=std::strtof(argv[1],&end);
  if(errno||end==argv[1]||*end||!std::isfinite(altitudeM))return 2;
  instrument::AltimeterRenderer renderer;
  if(!renderer.begin())return 1;
  std::vector<uint16_t> frame(466*466);
  const auto began=std::chrono::steady_clock::now();
  renderer.render(frame.data(),altitudeM);
  instrument::FpsOverlay overlay;
  overlay.blit(frame.data(),466);
  overlay.blitSimBand(frame.data(),466,0,466,0,true);
  std::fprintf(stderr,"Render %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count());
  FILE* file=std::fopen(argv[2],"wb");if(!file)return 1;
  const int stride=(466*3+3)&~3;
  std::fputc('B',file);std::fputc('M',file);u32(file,54+stride*466);u32(file,0);u32(file,54);
  u32(file,40);u32(file,466);u32(file,466);u16(file,1);u16(file,24);u32(file,0);u32(file,stride*466);u32(file,2835);u32(file,2835);u32(file,0);u32(file,0);
  for(int y=465;y>=0;--y){
    for(int x=0;x<466;++x){uint16_t c=frame[y*466+x];std::fputc((c&31)*255/31,file);std::fputc(((c>>5)&63)*255/63,file);std::fputc((c>>11)*255/31,file);}
    for(int i=466*3;i<stride;++i)std::fputc(0,file);
  }
  bool failed=std::ferror(file);if(std::fclose(file))failed=true;return failed?1:0;
}
