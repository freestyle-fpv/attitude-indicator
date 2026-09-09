#include "fast_panel.h"
#include <driver/gpio.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

struct spi_device_t {};
namespace {
void require(bool condition,const char* message){
  if(!condition)throw std::runtime_error(message);
}
struct InspectablePanel : instrument::PanelDisplay {
  void cacheWindow(){_currentX=123;_currentY=234;}
  bool invalidated()const{return _currentX==-1 && _currentY==-1;}
};
struct Pending {
  spi_transaction_t* descriptor;
  spi_transaction_ext_t snapshot;
};
struct MockSpi {
  spi_device_t device;
  int queueDepth=0,cs=1;
  bool owned=false,failAdd=false;
  unsigned commands=0,queued=0,completed=0,reused=0;
  size_t maxPending=0,bytesQueued=0;
  uint16_t columns[2]={},rows[2]={};
  InspectablePanel* display=nullptr;
  const uint8_t* source=nullptr;
  std::deque<Pending> pending;
  std::vector<spi_transaction_t*> seen;
  std::vector<int> edges;
  std::vector<uint8_t> expected,received;
  std::string error;
  void checkHandle(spi_device_handle_t handle){require(handle==&device,"wrong SPI handle");}
  void checkPending(){
    for(const Pending& item:pending){
      const auto& now=*reinterpret_cast<const spi_transaction_ext_t*>(item.descriptor);
      const auto& old=item.snapshot;
      require(now.base.flags==old.base.flags && now.base.cmd==old.base.cmd &&
        now.base.addr==old.base.addr && now.base.length==old.base.length &&
        now.base.tx_buffer==old.base.tx_buffer && now.command_bits==old.command_bits &&
        now.address_bits==old.address_bits && now.dummy_bits==old.dummy_bits,
        "DMA descriptor overwritten before get-result");
    }
  }
} spi;

// Independent pixel enumeration, without using production circleBandBounds().
int expectedLeft(int first,int rows){
  int left=232;
  for(int y=first;y<first+rows;++y)for(int x=0;x<left;++x){
    const double dx=x-232.5,dy=y-232.5;
    if(dx*dx+dy*dy<=232.5*232.5){left=x;break;}
  }
  return left&~1;
}

size_t checkBand(instrument::FastPanel& panel,InspectablePanel& display,int first,int rows){
  const int left=expectedLeft(first,rows),width=466-2*left;
  const size_t pixels=static_cast<size_t>(width)*rows;
  std::vector<uint16_t> guarded(pixels+4,0xa53c);
  uint16_t* frame=guarded.data()+2;
  uint32_t random=0x5941cf21u+first+rows;
  for(size_t i=0;i<pixels;++i){random=random*1664525u+1013904223u;frame[i]=random>>8;}
  const auto unchanged=guarded;
  require(!spi.owned && spi.pending.empty() && spi.cs==1,"previous band incomplete");
  spi.commands=spi.queued=spi.completed=spi.reused=0;
  spi.maxPending=spi.bytesQueued=0;
  spi.edges.clear();spi.seen.clear();spi.received.clear();
  spi.columns[0]=6+left;spi.columns[1]=6+left+width-1;
  spi.rows[0]=first;spi.rows[1]=first+rows-1;
  spi.display=&display;spi.source=reinterpret_cast<const uint8_t*>(frame);
  spi.expected.assign(spi.source,spi.source+pixels*2);
  display.cacheWindow();
  panel.presentBand(display,frame,first,rows);
  require(display.invalidated(),"vendor window cache left stale");
  require(!spi.owned && spi.pending.empty() && spi.cs==1,"band returned before DMA drained");
  require(spi.commands==2 && spi.queued==spi.completed,"incomplete commands or DMA results");
  require(spi.edges==std::vector<int>({0,1,0,1,0,1}),"incorrect CS command/payload framing");
  require(spi.bytesQueued==pixels*2 && spi.received==spi.expected,"compact payload byte mismatch");
  require(guarded==unchanged,"source pixels or buffer guards modified");
  if(first==0 && rows==466){
    require(spi.queued>8 && spi.maxPending==8 && spi.reused>0,"full frame did not exercise queue reuse");
  }
  return pixels*2;
}
}

uint32_t micros(){static uint32_t tick=0;return ++tick;}
void fastPanelCheckError(esp_err_t result){
  if(result!=ESP_OK)throw std::runtime_error(spi.error.empty()?"unexpected SPI error":spi.error);
}
esp_err_t spi_bus_add_device(int host,const spi_device_interface_config_t* config,spi_device_handle_t* handle){
  require(host==SPI2_HOST && config->command_bits==8 && config->address_bits==24 &&
    config->dummy_bits==0 && config->mode==0 && config->clock_speed_hz==40000000 &&
    config->spics_io_num==-1 && config->flags==SPI_DEVICE_HALFDUPLEX && config->queue_size==8,
    "incorrect QSPI device configuration");
  if(spi.failAdd)return ESP_ERR_INVALID_ARG;
  spi.queueDepth=config->queue_size;*handle=&spi.device;return ESP_OK;
}
esp_err_t spi_device_acquire_bus(spi_device_handle_t handle,TickType_t wait){
  spi.checkHandle(handle);
  require(wait==portMAX_DELAY && !spi.owned && spi.cs==1 && spi.pending.empty(),"invalid bus acquisition");
  spi.owned=true;return ESP_OK;
}
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t level){
  require(pin==12 && spi.owned && level<=1 && int(level)!=spi.cs,"invalid CS transition");
  spi.checkPending();
  if(level)require(spi.pending.empty(),"CS released before DMA completion");
  spi.cs=level;spi.edges.push_back(level);return ESP_OK;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,spi_transaction_t* tx){
  spi.checkHandle(handle);
  require(spi.owned && spi.cs==0 && spi.pending.empty() && spi.commands<2,"window command outside owned bus");
  require(spi.display->invalidated(),"window cache not invalidated before raw commands");
  require(tx->cmd==0x02 && tx->addr==(spi.commands==0?0x002A00u:0x002B00u) && tx->length==32 &&
    tx->flags==(SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR|SPI_TRANS_USE_TXDATA),"incorrect window command prefix");
  const uint16_t* bounds=spi.commands==0?spi.columns:spi.rows;
  for(int i=0;i<2;++i)require(tx->tx_data[2*i]==uint8_t(bounds[i]>>8) &&
    tx->tx_data[2*i+1]==uint8_t(bounds[i]),"incorrect panel window coordinates");
  ++spi.commands;return ESP_OK;
}
esp_err_t spi_device_queue_trans(spi_device_handle_t handle,spi_transaction_t* tx,TickType_t wait){
  spi.checkHandle(handle);spi.checkPending();
  require(wait==portMAX_DELAY && spi.owned && spi.cs==0 && spi.commands==2,"DMA outside color write");
  // Match both independent constraints enforced by ESP-IDF, not just bus allocation.
  if(tx->length>SPI_LL_DMA_MAX_BIT_LEN){
    spi.error="DMA exceeds ESP32-S3 hardware limit (262144 bits)";return ESP_ERR_INVALID_ARG;
  }
  if(tx->length>(ESP32QSPI_MAX_PIXELS_AT_ONCE*16u+8u)*8u){
    spi.error="DMA exceeds configured QSPI bus capacity";return ESP_ERR_INVALID_ARG;
  }
  require(tx->length>0 && tx->length%16==0 && spi.pending.size()<size_t(spi.queueDepth),"invalid DMA size or queue overflow");
  for(const auto& item:spi.pending)require(item.descriptor!=tx,"queued descriptor reused before get-result");
  const auto& ext=*reinterpret_cast<const spi_transaction_ext_t*>(tx);
  if(spi.queued==0)require(tx->flags==SPI_TRANS_MODE_QIO && tx->cmd==0x32 && tx->addr==0x002C00,"missing first color-write prefix");
  else require(tx->flags==(SPI_TRANS_MODE_QIO|SPI_TRANS_VARIABLE_CMD|SPI_TRANS_VARIABLE_ADDR|SPI_TRANS_VARIABLE_DUMMY) &&
    tx->cmd==0 && tx->addr==0 && ext.command_bits==0 && ext.address_bits==0 && ext.dummy_bits==0,
    "continuation emits a command/address/dummy prefix");
  const size_t bytes=tx->length/8;
  require(spi.bytesQueued+bytes<=spi.expected.size() && tx->tx_buffer==spi.source+spi.bytesQueued,
    "DMA does not cover the compact source contiguously");
  require(std::memcmp(tx->tx_buffer,spi.expected.data()+spi.bytesQueued,bytes)==0,"DMA source bytes changed");
  if(std::find(spi.seen.begin(),spi.seen.end(),tx)!=spi.seen.end())++spi.reused;
  else spi.seen.push_back(tx);
  spi.pending.push_back({tx,ext});spi.maxPending=std::max(spi.maxPending,spi.pending.size());
  spi.bytesQueued+=bytes;++spi.queued;return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,spi_transaction_t** done,TickType_t wait){
  spi.checkHandle(handle);spi.checkPending();
  require(wait==portMAX_DELAY && spi.owned && spi.cs==0 && !spi.pending.empty(),"invalid DMA result wait");
  // DMA consumes memory on completion, so early descriptor/source mutation is observable.
  const auto& item=spi.pending.front();
  const auto* bytes=static_cast<const uint8_t*>(item.descriptor->tx_buffer);
  spi.received.insert(spi.received.end(),bytes,bytes+item.descriptor->length/8);
  *done=item.descriptor;spi.pending.pop_front();++spi.completed;return ESP_OK;
}
void spi_device_release_bus(spi_device_handle_t handle){
  spi.checkHandle(handle);
  require(spi.owned && spi.cs==1 && spi.pending.empty() && spi.completed==spi.queued,"bus released before DMA drained");
  spi.owned=false;
}

int main(){
  try{
    spi.failAdd=true;instrument::FastPanel unavailable;
    require(!unavailable.begin(),"begin ignored SPI device allocation failure");
    spi.failAdd=false;instrument::FastPanel panel;InspectablePanel display;
    require(panel.begin(),"begin failed");
    const int bands[][2]={{0,1},{465,1},{1,2},{462,3},{0,60},{60,60},{120,60},
      {180,60},{240,60},{300,60},{360,60},{420,46},{0,118},{348,118},{232,1},
      {231,2},{17,37},{111,156},{0,466}};
    size_t totalBytes=0,cases=0;
    for(const auto& band:bands){totalBytes+=checkBand(panel,display,band[0],band[1]);++cases;}
    uint32_t random=0x516e93a7;
    for(int i=0;i<32;++i){
      random=random*1664525u+1013904223u;const int first=random%466;
      random=random*1664525u+1013904223u;const int rows=1+random%(466-first);
      totalBytes+=checkBand(panel,display,first,rows);++cases;
    }
    std::printf("FastPanel macro=%d: %zu bands, %zu compact payload bytes; windows, prefixes, DMA limits, FIFO lifetime, CS/bus ownership, unchanged source PASS\n",
      ESP32QSPI_MAX_PIXELS_AT_ONCE,cases,totalBytes);
  }catch(const std::exception& error){std::fprintf(stderr,"FastPanel FAIL: %s\n",error.what());return 1;}
}
