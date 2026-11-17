#pragma once
#include "panel_display.h"
#include "installation.h"
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
  // Copy packed PSRAM pixels into two alternating internal DMA chunks. A null
  // scratch is allowed for panel-order pixels already in internal DMA memory.
  void presentRect(PanelDisplay& display,const uint16_t* pixels,int x,int y,int width,int height,uint16_t* scratch,size_t scratchPixels,UsbSide side=UsbSide::Left);
  void presentRegions(PanelDisplay&,const uint16_t*,const AirspeedRect*,int,uint16_t*,size_t);
  void enableTearingSignal();
  struct RegionTiming { volatile uint32_t began=0,ended=0; };
  const RegionTiming& regionTiming(unsigned i)const{return regionTiming_[i];}
  uint32_t setupMicros() const{return setupUs_;}
 private:
  void presentWindow(PanelDisplay&,const uint16_t*,int,int,int,int,uint16_t*,size_t,UsbSide side=UsbSide::Left);
  void command(uint8_t code,const uint8_t* data,size_t bytes);
  static void beginQueuedTransaction(spi_transaction_t*);
  static void endQueuedTransaction(spi_transaction_t*);
  // Both the vendor bus allocation and the peripheral bit-length register
  // constrain a transaction. S3 supports at most 32768 bytes per DMA transfer.
  static constexpr size_t BusChunkPixels=ESP32QSPI_MAX_PIXELS_AT_ONCE*8u;
  static constexpr size_t HardwareChunkPixels=SPI_LL_DMA_MAX_BIT_LEN/16u;
  static constexpr size_t ChunkPixels=BusChunkPixels<HardwareChunkPixels?BusChunkPixels:HardwareChunkPixels;
  static constexpr unsigned QueueDepth=8;
  spi_device_handle_t handle_=nullptr;
  spi_transaction_ext_t transactions_[QueueDepth]={};
  RegionTiming regionTiming_[40];
  uint32_t setupUs_=0;
};
}
