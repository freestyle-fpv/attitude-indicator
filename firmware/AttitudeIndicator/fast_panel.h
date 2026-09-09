#pragma once
#include "panel_display.h"
#include <driver/spi_master.h>
#include <hal/spi_ll.h>
namespace instrument {
class FastPanel {
 public:
  bool begin();
  static constexpr size_t chunkPixels(){return ChunkPixels;}
  // Pixels are already compacted to circleBandBounds() in panel byte order.
  // The caller retains exclusive ownership until all DMA completes.
  void presentBand(PanelDisplay& display,uint16_t* pixels,int firstRow,int rows);
  uint32_t setupMicros() const{return setupUs_;}
 private:
  void command(uint8_t code,const uint8_t* data,size_t bytes);
  // Both the vendor bus allocation and the peripheral bit-length register
  // constrain a transaction. S3 supports at most 32768 bytes per DMA transfer.
  static constexpr size_t BusChunkPixels=ESP32QSPI_MAX_PIXELS_AT_ONCE*8u;
  static constexpr size_t HardwareChunkPixels=SPI_LL_DMA_MAX_BIT_LEN/16u;
  static constexpr size_t ChunkPixels=BusChunkPixels<HardwareChunkPixels?BusChunkPixels:HardwareChunkPixels;
  static constexpr unsigned QueueDepth=8;
  spi_device_handle_t handle_=nullptr;
  spi_transaction_ext_t transactions_[QueueDepth]={};
  uint32_t setupUs_=0;
};
}
