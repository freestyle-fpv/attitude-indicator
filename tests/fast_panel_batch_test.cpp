#include "fast_panel.h"
#include "airspeed_renderer.h"
#include <driver/gpio.h>
#include <deque>
#include <vector>
#include <cstring>
#include <cstdio>
#include <stdexcept>
using namespace instrument;
struct spi_device_t{};
namespace {
void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Pending{spi_transaction_t* pointer;spi_transaction_ext_t copy;};
spi_device_t device;spi_device_interface_config_t config{};bool owned=false;int cs=1,phase=0;
size_t offset=0,regionBytes=0,regionIndex=0,maxPending=0;unsigned acquisitions=0;
std::deque<Pending> pending;std::vector<AirspeedRect> regions;std::vector<uint8_t> expected,received;
void consume(spi_transaction_t* tx){
  check(owned&&cs==0,"transaction without bus/CS ownership");const auto r=regions.at(regionIndex);
  if(phase<2){check(tx->cmd==2&&tx->addr==(phase==0?0x2A00u:0x2B00u)&&tx->length==32,"wrong window prefix");
    check(tx->flags==(SPI_TRANS_MULTILINE_CMD|SPI_TRANS_MULTILINE_ADDR|SPI_TRANS_USE_TXDATA),"wrong window flags");
    const int first=phase==0?r.x+6:r.y,last=first+(phase==0?r.width:r.height)-1;
    check(tx->tx_data[0]==first/256&&tx->tx_data[1]==first%256&&tx->tx_data[2]==last/256&&tx->tx_data[3]==last%256,"wrong window bounds");++phase;
  }else{check(tx->length>0&&tx->length%16==0&&tx->length<=SPI_LL_DMA_MAX_BIT_LEN&&tx->length<=(ESP32QSPI_MAX_PIXELS_AT_ONCE*16u+8u)*8u,"invalid DMA length");
    check(regionBytes?bool(tx->flags&SPI_TRANS_VARIABLE_CMD):(tx->cmd==0x32&&tx->addr==0x2C00&&(tx->flags&SPI_TRANS_MODE_QIO)),"wrong pixel prefix");
    const size_t bytes=tx->length/8;check(offset+bytes<=expected.size(),"payload overflow");
    check(!std::memcmp(tx->tx_buffer,expected.data()+offset,bytes),"staging memory overwritten before completion");
    const auto* p=static_cast<const uint8_t*>(tx->tx_buffer);received.insert(received.end(),p,p+bytes);offset+=bytes;regionBytes+=bytes;
    if(regionBytes==size_t(r.width)*r.height*2){regionBytes=0;phase=0;++regionIndex;}
  }
}
void run(FastPanel& panel,const std::vector<AirspeedRect>& windows,bool batching){
  regions=windows;offset=regionBytes=regionIndex=maxPending=acquisitions=0;phase=0;expected.clear();received.clear();
  size_t pixels=0;for(auto r:regions)pixels+=r.width*r.height;
  std::vector<uint16_t> input(pixels+4,0xABCD),scratch(466*60+4,0xBEEF);
  uint32_t random=29;for(size_t i=0;i<pixels;++i){random=random*1664525u+1013904223u;input[i+2]=random>>12;}
  const auto original=input;const auto* p=reinterpret_cast<const uint8_t*>(input.data()+2);expected.assign(p,p+pixels*2);
  PanelDisplay display;panel.presentRegions(display,input.data()+2,regions.data(),regions.size(),scratch.data()+2,466*60);
  check(!owned&&cs==1&&pending.empty()&&regionIndex==regions.size()&&phase==0,"returned before final payload completed");
  check(received==expected&&input==original,"pixel/source mismatch");
  check(scratch[0]==0xBEEF&&scratch[1]==0xBEEF&&scratch[scratch.size()-1]==0xBEEF&&scratch[scratch.size()-2]==0xBEEF,"scratch guard overwritten");
  if(batching)check(acquisitions==1&&maxPending>=4&&maxPending<=8,"regions still submit and wait one at a time");
}
}
uint32_t micros(){static uint32_t t=0;return ++t;}
void fastPanelCheckError(esp_err_t result){check(result==ESP_OK,"SPI error");}
esp_err_t gpio_set_level(gpio_num_t pin,uint32_t value){check(pin==12&&owned&&int(value)!=cs,"bad CS edge");cs=value;return ESP_OK;}
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t* c,spi_device_handle_t* h){config=*c;*h=&device;return ESP_OK;}
esp_err_t spi_device_acquire_bus(spi_device_handle_t,TickType_t){check(!owned&&pending.empty()&&cs==1,"bus collision");owned=true;++acquisitions;return ESP_OK;}
void spi_device_release_bus(spi_device_handle_t){check(owned&&pending.empty()&&cs==1,"early release");owned=false;}
esp_err_t spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t* tx){check(pending.empty(),"polling over queued transfer");consume(tx);return ESP_OK;}
esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t* tx,TickType_t){
  check(owned&&pending.size()<size_t(config.queue_size),"queue overflow");for(auto& p:pending)check(p.pointer!=tx,"descriptor reused while pending");
  pending.push_back({tx,*reinterpret_cast<spi_transaction_ext_t*>(tx)});maxPending=std::max(maxPending,pending.size());return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t** tx,TickType_t){
  check(!pending.empty(),"empty queue");auto p=pending.front();pending.pop_front();
  const auto& now=*reinterpret_cast<const spi_transaction_ext_t*>(p.pointer);const auto& old=p.copy;
  check(now.base.flags==old.base.flags&&now.base.cmd==old.base.cmd&&now.base.addr==old.base.addr&&now.base.length==old.base.length&&now.base.user==old.base.user&&now.command_bits==old.command_bits&&now.address_bits==old.address_bits&&now.dummy_bits==old.dummy_bits,"descriptor changed before completion");
  check(now.base.flags&SPI_TRANS_USE_TXDATA?!std::memcmp(now.base.tx_data,old.base.tx_data,4):now.base.tx_buffer==old.base.tx_buffer,"transaction payload changed before completion");
  if(config.pre_cb)config.pre_cb(p.pointer);consume(p.pointer);if(config.post_cb)config.post_cb(p.pointer);*tx=p.pointer;return ESP_OK;
}
int main(){try{FastPanel panel;check(panel.begin(),"begin");
  run(panel,{{16,16,32,16},{80,32,64,32},{16,80,128,48},{180,432,106,22},{400,448,32,16}},true);
  run(panel,{{0,0,466,466}},false);
  std::puts("PASS queued region batch, windows, DMA staging lifetime, source guards, fallback and final completion");
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
