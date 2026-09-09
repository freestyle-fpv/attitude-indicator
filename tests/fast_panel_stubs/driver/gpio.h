#pragma once
#include "spi_master.h"
using gpio_num_t=int;
esp_err_t gpio_set_level(gpio_num_t,uint32_t);
