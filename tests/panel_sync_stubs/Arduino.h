#pragma once
#include <cstdint>
#define INPUT 0
#define RISING 1
#define ARDUINO_ISR_ATTR
extern uint32_t fakeNow;
extern void (*fakeEdge)(void*);
extern void* fakeArg;
inline uint32_t micros(){return fakeNow;}
inline void delayMicroseconds(uint32_t n){fakeNow+=n;}
inline void pinMode(int,int){}
inline void attachInterruptArg(int,void(*fn)(void*),void* arg,int){fakeEdge=fn;fakeArg=arg;}
