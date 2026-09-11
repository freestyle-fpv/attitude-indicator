#include "calibration_controls.h"
#include <cassert>
#include <cstdio>
using namespace instrument;
int main(){
  CalibrationButton b;
  assert(!b.update(true,false,0));
  assert(!b.update(true,true,100));assert(!b.update(true,false,110)); // bounce
  assert(!b.update(true,true,120));assert(!b.update(true,true,150));
  assert(!b.update(true,false,220));assert(b.update(true,false,250));
  assert(!b.update(true,false,300)); // only once per click
  assert(!b.update(true,true,400));assert(!b.update(true,true,430));
  assert(!b.update(true,true,2000));assert(!b.update(true,false,2010));
  assert(!b.update(true,false,2040)); // long press has no release action
  assert(!b.update(true,true,2200));assert(!b.update(true,true,2230));
  assert(!b.update(false,false,2250));assert(!b.update(true,false,2280)); // I2C failure cancels
  CalibrationButton held;assert(!held.update(true,true,0));assert(!held.update(true,false,100));
  assert(!held.update(true,false,130)); // power-on held button never calibrates
  CalibrationButton wrap;assert(!wrap.update(true,false,0xffffff00u));
  assert(!wrap.update(true,true,0xffffffc0u));assert(!wrap.update(true,true,0xffffffe0u));
  assert(!wrap.update(true,false,20));assert(wrap.update(true,false,50));
  CalibrationSession session;
  const auto first=session.request();assert(first&&session.active());
  assert(session.request()==0); // no restart storm while waiting for stillness
  assert(!session.observe(0,true)); // old already-calibrated snapshot cannot complete a new request
  assert(!session.observe(first,false));assert(session.active());
  assert(session.observe(first,true));assert(!session.active());assert(!session.observe(first,true));
  const auto second=session.request();assert(second!=first);
  assert(!session.observe(first,true));assert(session.observe(second,true));
  std::puts("PASS calibration click debounce/hold/fault/wrap and shared calibration acknowledgement");
}
