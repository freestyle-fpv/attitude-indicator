#ifndef INSTRUMENT_RENDERER_H
#define INSTRUMENT_RENDERER_H
#include <stdint.h>
namespace instrument {
class Renderer {
 public:
  Renderer();
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  bool begin();
  void render(uint16_t* rgb565, float pitchDegrees, float rollDegrees);
  void renderBand(uint16_t* rgb565, float pitchDegrees, float rollDegrees, int firstRow, int rowCount);
 private:
  struct Data;
  Data* data_;
};
}
#endif
