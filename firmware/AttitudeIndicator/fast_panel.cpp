#include "fast_panel.h"
#include "board_config.h"
#include "circle_band.h"
#include <esp_heap_caps.h>
#include <driver/gpio.h>
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
void FastPanel::presentBand(PanelDisplay& display,uint16_t* frame,int firstRow,int rows){
  assert(firstRow>=0&&rows>0&&firstRow+rows<=466);
  const CircleBand band=circleBandBounds(firstRow,rows);
  const uint32_t setupStart=micros();
  const size_t total=static_cast<size_t>(band.width)*rows;
  // Renderer produces panel byte order directly in exclusive internal DMA RAM.
  ESP_ERROR_CHECK(spi_device_acquire_bus(handle_,portMAX_DELAY));
  display.invalidateWindow();
  const uint16_t x=board::LcdColumnOffset+band.x,lastX=x+band.width-1;
  const uint16_t y=firstRow+board::LcdRowOffset,lastY=y+rows-1;
  const uint8_t columns[]={uint8_t(x>>8),uint8_t(x),uint8_t(lastX>>8),uint8_t(lastX)};
  const uint8_t lines[]={uint8_t(y>>8),uint8_t(y),uint8_t(lastY>>8),uint8_t(lastY)};
  command(0x2A,columns,sizeof(columns));
  command(0x2B,lines,sizeof(lines));
  setupUs_=micros()-setupStart;
  gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),0);
  size_t offset=0;unsigned chunk=0,pending=0;
  while(offset<total){
    const unsigned slot=chunk%QueueDepth;
    if(pending==QueueDepth){
      spi_transaction_t* done=nullptr;
      ESP_ERROR_CHECK(spi_device_get_trans_result(handle_,&done,portMAX_DELAY));
      // FIFO results correspond to this slot, which can now be overwritten.
      assert(done==&transactions_[slot].base);--pending;
    }
    const size_t pixels=std::min(ChunkPixels,total-offset);
    auto& tx=transactions_[slot];
    tx={};
    tx.base.flags=SPI_TRANS_MODE_QIO;
    if(chunk==0){tx.base.cmd=0x32;tx.base.addr=0x002C00;}
    else tx.base.flags|=SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY;
    tx.base.tx_buffer=frame+offset;tx.base.length=pixels*16;
    ESP_ERROR_CHECK(spi_device_queue_trans(handle_,&tx.base,portMAX_DELAY));
    ++pending;++chunk;offset+=pixels;
  }
  while(pending){spi_transaction_t* done=nullptr;ESP_ERROR_CHECK(spi_device_get_trans_result(handle_,&done,portMAX_DELAY));--pending;}
  gpio_set_level(static_cast<gpio_num_t>(board::LcdCs),1);
  spi_device_release_bus(handle_);
}
}
