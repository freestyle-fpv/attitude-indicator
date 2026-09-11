#pragma once
#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
namespace instrument {
// CO5300 TE mode 1 rises at vertical blank. Only the display task waits on it.
class PanelSync {
 public:
  void begin(int pin){
    waiter_=xTaskGetCurrentTaskHandle();pinMode(pin,INPUT);
    attachInterruptArg(pin,edge,this,RISING);
  }
  // HUD writes trail the current scan; other dials pass 0 for blank-start.
  bool wait(uint32_t phaseUs=4000){
    // A pulse left over while drawing cannot authorize a late scanout write.
    ulTaskNotifyTake(pdTRUE,0);
    if(ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(30))){
      const uint32_t edgeAt=last_.load(std::memory_order_relaxed);
      if(phaseUs>=2000)vTaskDelay(pdMS_TO_TICKS(phaseUs/1000-1));
      const uint32_t elapsed=micros()-edgeAt;
      if(elapsed<phaseUs)delayMicroseconds(phaseUs-elapsed);
      return true;
    }
    ++misses_;return false; // A disconnected TE must never freeze the interface.
  }
  uint32_t lastEdge()const{return last_.load(std::memory_order_relaxed);}
  uint32_t period()const{return period_.load(std::memory_order_relaxed);}
  uint32_t pulses()const{return pulses_.load(std::memory_order_relaxed);}
  uint32_t misses()const{return misses_;}
 private:
  static void ARDUINO_ISR_ATTR edge(void* arg){
    auto& s=*static_cast<PanelSync*>(arg);
    const uint32_t now=micros(),previous=s.last_.load(std::memory_order_relaxed);
    if(previous)s.period_.store(now-previous,std::memory_order_relaxed);
    s.last_.store(now,std::memory_order_relaxed);s.pulses_.fetch_add(1,std::memory_order_relaxed);
    BaseType_t wake=pdFALSE;vTaskNotifyGiveFromISR(s.waiter_,&wake);
    if(wake)portYIELD_FROM_ISR();
  }
  TaskHandle_t waiter_=nullptr;
  std::atomic<uint32_t> last_{0},period_{0},pulses_{0};
  uint32_t misses_=0;
};
}
