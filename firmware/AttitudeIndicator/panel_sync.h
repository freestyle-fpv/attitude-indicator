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
  bool wait(uint32_t phaseUs=4000,bool useRecentEdge=false,uint32_t latestUs=0){
    ulTaskNotifyTake(pdTRUE,0);
    uint32_t edgeAt=last_.load(std::memory_order_relaxed);
    // A fully prepared round update can have a bounded start window. Other
    // callers retain their fixed phase and the original pre-start lead time.
    constexpr uint32_t leadUs=400;
    const uint32_t age=micros()-edgeAt;
    const bool withinWindow=latestUs>=phaseUs&&latestUs>leadUs&&age<=latestUs-leadUs;
    const bool inTime=useRecentEdge && edgeAt && edgeAt!=usedEdge_ &&
                      ((phaseUs>leadUs&&age<phaseUs-leadUs)||withinWindow);
    if(!inTime){
      if(!ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(30))){
        ++misses_;return false; // A disconnected TE must never freeze the interface.
      }
      edgeAt=last_.load(std::memory_order_relaxed);
    }
    usedEdge_=edgeAt;
    const uint32_t elapsed=micros()-edgeAt;
    if(elapsed<phaseUs){
      const uint32_t remaining=phaseUs-elapsed;
      if(remaining>=2000)vTaskDelay(pdMS_TO_TICKS(remaining/1000-1));
      const uint32_t afterYield=micros()-edgeAt;
      if(afterYield<phaseUs)delayMicroseconds(phaseUs-afterYield);
    }
    return true;
  }
  uint32_t lastEdge()const{return last_.load(std::memory_order_relaxed);}
  uint32_t selectedEdge()const{return usedEdge_;}
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
  uint32_t misses_=0,usedEdge_=0;
};
}
