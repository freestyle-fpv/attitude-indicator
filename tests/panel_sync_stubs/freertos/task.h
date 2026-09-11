#pragma once
#include "FreeRTOS.h"
#include <Arduino.h>
extern uint32_t pulseTime;extern bool pulsePresent;
inline TaskHandle_t xTaskGetCurrentTaskHandle(){return nullptr;}
inline void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*){}
inline uint32_t ulTaskNotifyTake(int,uint32_t timeout){if(!timeout)return 0;if(!pulsePresent){fakeNow+=timeout*1000;return 0;}fakeNow=pulseTime;fakeEdge(fakeArg);return 1;}
inline void vTaskDelay(uint32_t ticks){fakeNow+=ticks*1000;}
