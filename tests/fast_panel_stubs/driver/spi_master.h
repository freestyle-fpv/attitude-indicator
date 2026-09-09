#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int;
using TickType_t=uint32_t;
constexpr esp_err_t ESP_OK=0, ESP_ERR_INVALID_ARG=0x102;
constexpr TickType_t portMAX_DELAY=0xffffffffu;
constexpr int SPI2_HOST=1;
constexpr uint32_t SPI_DEVICE_HALFDUPLEX=1u<<4;
constexpr uint32_t SPI_TRANS_MODE_QIO=1u<<1, SPI_TRANS_USE_TXDATA=1u<<3;
constexpr uint32_t SPI_TRANS_MULTILINE_ADDR=1u<<4, SPI_TRANS_VARIABLE_CMD=1u<<5;
constexpr uint32_t SPI_TRANS_VARIABLE_ADDR=1u<<6, SPI_TRANS_VARIABLE_DUMMY=1u<<7;
constexpr uint32_t SPI_TRANS_MULTILINE_CMD=1u<<9;
struct spi_device_t;
using spi_device_handle_t=spi_device_t*;
struct spi_transaction_t {
  uint32_t flags;
  uint16_t cmd;
  uint64_t addr;
  size_t length, rxlength;
  void* user;
  union { const void* tx_buffer; uint8_t tx_data[4]; };
  union { void* rx_buffer; uint8_t rx_data[4]; };
};
struct spi_transaction_ext_t {
  spi_transaction_t base;
  uint8_t command_bits, address_bits, dummy_bits;
};
struct spi_device_interface_config_t {
  uint8_t command_bits, address_bits, dummy_bits, mode;
  int clock_speed_hz, spics_io_num;
  uint32_t flags;
  int queue_size;
};
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t*);
esp_err_t spi_device_acquire_bus(spi_device_handle_t,TickType_t);
esp_err_t spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t*);
esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t*,TickType_t);
esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t**,TickType_t);
void spi_device_release_bus(spi_device_handle_t);
void fastPanelCheckError(esp_err_t);
#define ESP_ERROR_CHECK(call) fastPanelCheckError(call)
