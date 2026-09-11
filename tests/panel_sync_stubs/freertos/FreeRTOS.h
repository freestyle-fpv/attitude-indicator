#pragma once
#include <cstdint>
using BaseType_t=int;using TickType_t=uint32_t;using TaskHandle_t=void*;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(n) (n)
#define portYIELD_FROM_ISR() ((void)0)
