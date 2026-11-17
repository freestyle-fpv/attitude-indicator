#include "fast_panel.h"
#include "board_config.h"
#include "circle_band.h"
#include "airspeed_renderer.h"
#include <esp_heap_caps.h>
#include <driver/gpio.h>
#include <cstring>
#ifdef ARDUINO
#include <esp_attr.h>
#define PANEL_IRAM IRAM_ATTR
#else
#define PANEL_IRAM
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")
#endif
namespace instrument {
bool FastPanel::begin(){
  spi_device_interface_config_t config={};
  config.command_bits=8;config.address_bits=24;
  config.clock_speed_hz=40000000;
  config.mode=0;config.spics_io_num=-1;
  config.flags=SPI_DEVICE_HALFDUPLEX;config.queue_size=QueueDepth;
  config.pre_cb=beginQueuedTransaction;config.post_cb=endQueuedTransaction;
  if(spi_bus_add_device(SPI2_HOST,&config,&handle_)!=ESP_OK)return false;
  return true;
}
void FastPanel::command(uint8_t code,const uint8_t* data,size_t bytes){
  assert(bytes<=4);
  spi_transaction_t tx={};
  tx.cmd=0x02;tx.addr=uint32_t(code)<<8;
  tx.flags=SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR;
  if(bytes){tx.flags|=SPI_TRANS_USE_TXDATA;for(size_t i=0;i<bytes;++i)tx.tx_data[i]=data[i];}
  tx.length=bytes*8;
  gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),0);
  ESP_ERROR_CHECK(spi_device_polling_transmit(handle_,&tx));
  gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),1);
}
void FastPanel::enableTearingSignal(){
  ESP_ERROR_CHECK(spi_device_acquire_bus(handle_,portMAX_DELAY));
  const uint8_t mode=0;command(0x35,&mode,1);
  spi_device_release_bus(handle_);
}
void FastPanel::presentRect(PanelDisplay& display,const uint16_t* frame,int x,int y,int width,int height,uint16_t* scratch,size_t scratchPixels,UsbSide side){
  assert(scratch?scratchPixels>=4:side==UsbSide::Left);
  assert((x|y|width|height)%2==0); // CO5300 windows start even and end odd on BOTH axes.
  const auto r=panelRect({x,y,width,height},side);
  presentWindow(display,frame,r.x,r.y,r.width,r.height,scratch,scratchPixels,side);
}
void PANEL_IRAM FastPanel::beginQueuedTransaction(spi_transaction_t* tx){
  if(tx->user){gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),0);
    if(tx->cmd==0x32)static_cast<RegionTiming*>(tx->user)->began=micros();}
}
void PANEL_IRAM FastPanel::endQueuedTransaction(spi_transaction_t* tx){
  if(tx->user){if(tx->cmd==0x32)static_cast<RegionTiming*>(tx->user)->ended=micros();
    gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),1);}
}
void FastPanel::presentRegions(PanelDisplay& display,const uint16_t* pixels,const AirspeedRect* regions,int count,uint16_t* scratch,size_t scratchPixels){
  for(auto& timing:regionTiming_){timing.began=0;timing.ended=0;}
  const size_t limit=std::min(ChunkPixels,(scratchPixels/2)&~size_t(1));
  bool batch=scratch&&count>1;
  for(int i=0;i<count;++i){const auto r=regions[i];assert(r.x>=0&&r.y>=0&&r.width>0&&r.height>0&&r.x+r.width<=466&&r.y+r.height<=466&&((r.x|r.y|r.width|r.height)&1)==0);
    if(size_t(r.width)*r.height>limit)batch=false;}
  if(!batch){size_t offset=0;uint32_t setup=0;for(int i=0;i<count;++i){const auto r=regions[i];
      presentRect(display,pixels+offset,r.x,r.y,r.width,r.height,scratch,scratchPixels);offset+=r.width*r.height;setup+=setupUs_;}
    setupUs_=setup;return;}
  // Two complete window groups (column, row, pixels) stay in flight. ISR
  // callbacks frame each packet; the CPU can prepare the next window while
  // DMA consumes the previous one. Reuse a group only after all three finish.
  ESP_ERROR_CHECK(spi_device_acquire_bus(handle_,portMAX_DELAY));display.invalidateWindow();setupUs_=0;
  unsigned pending=0,completed=0;size_t offset=0;
  auto drain=[&](){const unsigned slot=completed%2;
    for(unsigned j=0;j<3;++j){spi_transaction_t* done=nullptr;ESP_ERROR_CHECK(spi_device_get_trans_result(handle_,&done,portMAX_DELAY));assert(done==&transactions_[slot*3+j].base);}
    ++completed;--pending;};
  for(int i=0;i<count;++i){if(pending==2)drain();const uint32_t began=micros();
    const auto r=regions[i];const unsigned slot=i%2;const size_t size=size_t(r.width)*r.height;
    copyPixels(scratch+slot*limit,pixels+offset,size);
    for(unsigned j=0;j<3;++j){auto& tx=transactions_[slot*3+j];tx={};tx.base.user=&regionTiming_[i];
      if(j<2){const unsigned first=j==0?r.x+board::LcdColumnOffset:r.y+board::LcdRowOffset;
        const unsigned last=first+(j==0?r.width:r.height)-1;
        tx.base.cmd=2;tx.base.addr=j==0?0x002A00:0x002B00;tx.base.length=32;
        tx.base.flags=SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR|SPI_TRANS_USE_TXDATA;
        tx.base.tx_data[0]=first>>8;tx.base.tx_data[1]=first;tx.base.tx_data[2]=last>>8;tx.base.tx_data[3]=last;
      }else{tx.base.cmd=0x32;tx.base.addr=0x002C00;tx.base.flags=SPI_TRANS_MODE_QIO;tx.base.length=size*16;tx.base.tx_buffer=scratch+slot*limit;}
      ESP_ERROR_CHECK(spi_device_queue_trans(handle_,&tx.base,portMAX_DELAY));
    }
    ++pending;offset+=size;setupUs_+=micros()-began;
  }
  while(pending)drain();spi_device_release_bus(handle_);
}
void FastPanel::presentBand(PanelDisplay& display,uint16_t* frame,int firstRow,int rows){
  assert(firstRow>=0&&rows>0&&firstRow+rows<=466);
  const CircleBand band=circleBandBounds(firstRow,rows);
  presentWindow(display,frame,band.x,firstRow,band.width,rows,nullptr,0);
}
void FastPanel::presentWindow(PanelDisplay& display,const uint16_t* frame,int left,int firstRow,int width,int rows,uint16_t* scratch,size_t scratchPixels,UsbSide side){
  assert(frame&&left>=0&&firstRow>=0&&width>0&&rows>0&&left+width<=466&&firstRow+rows<=466);
  const uint32_t setupStart=micros();
  const size_t total=static_cast<size_t>(width)*rows;
  // Renderer produces panel byte order directly in exclusive internal DMA RAM.
  ESP_ERROR_CHECK(spi_device_acquire_bus(handle_,portMAX_DELAY));
  display.invalidateWindow();
  const uint16_t x=board::LcdColumnOffset+left,lastX=x+width-1;
  const uint16_t y=firstRow+board::LcdRowOffset,lastY=y+rows-1;
  const uint8_t columns[]={uint8_t(x>>8),uint8_t(x),uint8_t(lastX>>8),uint8_t(lastX)};
  const uint8_t lines[]={uint8_t(y>>8),uint8_t(y),uint8_t(lastY>>8),uint8_t(lastY)};
  command(0x2A,columns,sizeof(columns));
  command(0x2B,lines,sizeof(lines));
  setupUs_=micros()-setupStart;
  gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),0);
  // A completed DMA descriptor AND its backing memory must be released before reuse.
  const unsigned depth=scratch?2:QueueDepth;
  const size_t limit=scratch?std::min(ChunkPixels,(scratchPixels/2)&~size_t(1)):ChunkPixels;
  const bool transposed=quarterTurn(side);
  size_t offset=0;unsigned chunk=0,pending=0;
  while(offset<total){
    const unsigned slot=chunk%depth;
    if(pending==depth){
      spi_transaction_t* done=nullptr;
      ESP_ERROR_CHECK(spi_device_get_trans_result(handle_,&done,portMAX_DELAY));
      // FIFO results correspond to this slot, which can now be overwritten.
      assert(done==&transactions_[slot].base);--pending;
    }
    const size_t pixels=std::min(limit,total-offset);
    auto& tx=transactions_[slot];
    tx={};
    tx.base.flags=SPI_TRANS_MODE_QIO;
    if(chunk==0){tx.base.cmd=0x32;tx.base.addr=0x002C00;}
    else tx.base.flags|=SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY;
    if(scratch){
      auto* out=scratch+slot*limit;
      copyPanelPixels(out,frame,transposed?rows:width,transposed?width:rows,side,offset,pixels);
      tx.base.tx_buffer=out;
    }
    else tx.base.tx_buffer=frame+offset;
    tx.base.length=pixels*16;
    ESP_ERROR_CHECK(spi_device_queue_trans(handle_,&tx.base,portMAX_DELAY));
    ++pending;++chunk;offset+=pixels;
  }
  while(pending){spi_transaction_t* done=nullptr;ESP_ERROR_CHECK(spi_device_get_trans_result(handle_,&done,portMAX_DELAY));--pending;}
  gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),1);
  spi_device_release_bus(handle_);
}
}
