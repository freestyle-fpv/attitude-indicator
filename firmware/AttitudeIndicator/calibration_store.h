#pragma once
#include "attitude.h"
#include "installation.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <atomic>

namespace instrument {
// Bump the schema when the sensor configuration or mounting mapping changes.
struct CalibrationRecord {
  uint32_t magic=0,schema=0,side=0;
  Vec3 bias{0,0,0},gravity{0,0,0};
  uint32_t checksum=0;
};
struct CalibrationSaveRequest {CalibrationRecord record;uint32_t token;};
// Low bits encode storage state; upper bits identify the calibration window.
inline int calibrationStorageCode(uint32_t state){const unsigned code=state&7u;return code==4?-1:int(code);}
inline bool finishCalibrationSave(std::atomic<uint32_t>& status,uint32_t token,bool saved){
  const uint32_t result=(token&~7u)|(saved?3u:4u);
  return status.compare_exchange_strong(token,result);
}
static_assert(sizeof(CalibrationRecord)==40,"calibration storage format must be stable");
inline uint32_t calibrationChecksum(const CalibrationRecord& record){
  const auto* bytes=reinterpret_cast<const unsigned char*>(&record);
  uint32_t crc=0xffffffffu;
  for(size_t i=0;i<offsetof(CalibrationRecord,checksum);++i){
    crc^=bytes[i];for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xedb88320u:0u);
  }
  return ~crc;
}
inline const char* calibrationKey(UsbSide side){
  const char* const keys[]={"cal-left","cal-top","cal-right","cal-bottom"};
  return unsigned(side)<4?keys[unsigned(side)]:nullptr;
}
inline CalibrationRecord makeCalibrationRecord(UsbSide side,Vec3 bias,Vec3 gravity){
  CalibrationRecord record;record.magic=0x43414c31u;record.schema=1;record.side=unsigned(side);
  record.bias=bias;record.gravity=gravity;record.checksum=calibrationChecksum(record);return record;
}
inline bool validCalibrationRecord(const CalibrationRecord& record,UsbSide side){
  if(unsigned(side)>=4||record.magic!=0x43414c31u||record.schema!=1||record.side!=unsigned(side)||record.checksum!=calibrationChecksum(record))return false;
  const auto b=record.bias,g=record.gravity;
  if(!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.z)||!std::isfinite(g.x)||!std::isfinite(g.y)||!std::isfinite(g.z))return false;
  const float raw=std::sqrt(b.x*b.x+b.y*b.y+b.z*b.z),gravity=std::sqrt(g.x*g.x+g.y*g.y+g.z*g.z);
  // Individual samples use 0.85..1.15g; allow the small shrinkage of a vector mean.
  return raw<=StationaryCalibration::MaxRawRateDps/57.295779513f&&gravity>.8f&&gravity<1.2f;
}
template<class Storage> bool loadCalibration(Storage& storage,UsbSide side,CalibrationRecord& output){
  const char* key=calibrationKey(side);CalibrationRecord candidate;
  if(!key||storage.getBytesLength(key)!=sizeof(candidate)||storage.getBytes(key,&candidate,sizeof(candidate))!=sizeof(candidate)||!validCalibrationRecord(candidate,side))return false;
  output=candidate;return true;
}
template<class Storage> bool saveCalibration(Storage& storage,const CalibrationRecord& record){
  const auto side=UsbSide(record.side);
  if(!validCalibrationRecord(record,side)||storage.putBytes(calibrationKey(side),&record,sizeof(record))!=sizeof(record))return false;
  CalibrationRecord readBack;
  return loadCalibration(storage,side,readBack)&&std::memcmp(&record,&readBack,sizeof(record))==0;
}
inline bool restoreCalibration(AttitudeFilter& filter,const CalibrationRecord& record,UsbSide side,Vec3 current,Vec3& bias){
  const float gravity=std::sqrt(current.x*current.x+current.y*current.y+current.z*current.z);
  if(!validCalibrationRecord(record,side)||!std::isfinite(gravity)||gravity<=.85f||gravity>=1.15f||!filter.restoreReference(record.gravity,current))return false;
  bias=record.bias;return true;
}
}
