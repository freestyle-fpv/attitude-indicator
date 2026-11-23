#include "../firmware/AttitudeIndicator/calibration_store.h"
#include <map>
#include <string>
#include <vector>
#include <limits>
#include <cstdio>
#include <cstring>
#include <atomic>
using namespace instrument;
struct Storage {
  std::map<std::string,std::vector<unsigned char>> bytes;
  bool fail=false,corrupt=false,shortRead=false;
  size_t getBytesLength(const char* key){return bytes[key].size();}
  size_t getBytes(const char* key,void* output,size_t count){
    const auto& data=bytes[key];if(data.size()!=count||shortRead)return 0;
    std::memcpy(output,data.data(),count);return count;
  }
  size_t putBytes(const char* key,const void* input,size_t count){
    if(fail)return 0;
    const auto* first=static_cast<const unsigned char*>(input);
    bytes[key]={first,first+count};if(corrupt)bytes[key][20]^=1;return count;
  }
};
static int failures=0;
static void check(bool ok,const char* name){std::printf("%s %s\n",ok?"PASS":"FAIL",name);if(!ok)++failures;}
static bool near(float a,float b){return std::fabs(a-b)<.02f;}
int main(){
  Storage storage;CalibrationRecord loaded;
  check(!loadCalibration(storage,UsbSide::Left,loaded),"fresh storage requires first calibration");
  const auto record=makeCalibrationRecord(UsbSide::Left,{.02f,-.01f,.03f},{0,-.342020143f,-.939692621f});
  check(saveCalibration(storage,record)&&loadCalibration(storage,UsbSide::Left,loaded),"complete calibration survives a storage write and fresh read");
  AttitudeFilter filter;Vec3 bias{0,0,0};
  check(restoreCalibration(filter,loaded,UsbSide::Left,{0,-.766044443f,-.642787610f},bias)&&near(filter.roll(),30)&&near(filter.pitch(),0)&&near(bias.x,.02f),
    "tilted boot restores the saved mounting zero and gyro bias without zeroing current tilt");
  const auto pitch=makeCalibrationRecord(UsbSide::Top,{0,0,0},{.258819045f,0,-.965925826f});
  check(restoreCalibration(filter,pitch,UsbSide::Top,{.642787610f,0,-.766044443f},bias)&&near(filter.pitch(),25),
    "saved pitch reference is independent of the current boot pitch");
  check(restoreCalibration(filter,record,UsbSide::Left,{.5f,-.296198133f,-.813797681f},bias)&&near(filter.pitch(),30)&&near(filter.roll(),0),
    "saved roll mounting trim does not turn boot pitch into false roll");
  check(!loadCalibration(storage,UsbSide::Top,loaded),"another installation direction cannot borrow an existing zero");
  for(unsigned i=0;i<4;++i){const auto side=UsbSide(i);const auto r=makeCalibrationRecord(side,{.01f*float(i),0,0},{0,0,-1});check(saveCalibration(storage,r),"each installation direction can save a separate record");}
  for(unsigned i=0;i<4;++i){check(loadCalibration(storage,UsbSide(i),loaded)&&near(loaded.bias.x,.01f*float(i)),"four direction records remain independent");}
  for(size_t i=0;i<sizeof(record);++i){auto damaged=record;reinterpret_cast<unsigned char*>(&damaged)[i]^=1;check(!validCalibrationRecord(damaged,UsbSide::Left),"any single-byte record corruption fails integrity validation");}
  auto obsolete=record;obsolete.schema=2;obsolete.checksum=calibrationChecksum(obsolete);
  check(!validCalibrationRecord(obsolete,UsbSide::Left),"unknown storage schema cannot restore even with a valid checksum");
  obsolete=record;obsolete.magic=0;obsolete.checksum=calibrationChecksum(obsolete);
  check(!validCalibrationRecord(obsolete,UsbSide::Left),"another blob format cannot restore even with a valid checksum");
  auto invalid=makeCalibrationRecord(UsbSide::Left,{std::numeric_limits<float>::quiet_NaN(),0,0},{0,0,-1});
  check(!saveCalibration(storage,invalid),"nonfinite bias is never persisted");
  invalid=makeCalibrationRecord(UsbSide::Left,{.2f,0,0},{0,0,-1});check(!saveCalibration(storage,invalid),"implausible saved gyro bias is rejected");
  invalid=makeCalibrationRecord(UsbSide::Left,{0,0,0},{0,0,0});check(!saveCalibration(storage,invalid),"zero gravity cannot authorize a saved mounting reference");
  invalid=makeCalibrationRecord(UsbSide(7),{0,0,0},{0,0,-1});check(!saveCalibration(storage,invalid),"invalid installation direction is rejected");
  storage.bytes[calibrationKey(UsbSide::Left)].resize(7);check(!loadCalibration(storage,UsbSide::Left,loaded),"truncated stored data triggers first calibration");
  storage.fail=true;check(!saveCalibration(storage,record),"storage write failure is reported");storage.fail=false;
  storage.corrupt=true;check(!saveCalibration(storage,record),"successful byte count cannot hide corrupt write-back");storage.corrupt=false;
  check(saveCalibration(storage,record),"a subsequent healthy storage write recovers");
  storage.shortRead=true;check(!loadCalibration(storage,UsbSide::Left,loaded),"short storage read cannot restore partial calibration");storage.shortRead=false;
  const float before=filter.pitch();bias={.04f,0,0};
  check(!restoreCalibration(filter,record,UsbSide::Left,{0,0,0},bias)&&near(filter.pitch(),before)&&near(bias.x,.04f),"untrusted startup acceleration preserves filter and bias until a valid sample arrives");
  check(!restoreCalibration(filter,record,UsbSide::Right,{0,0,-1},bias),"wrong-direction record cannot initialize an otherwise healthy IMU");
  std::atomic<uint32_t> status{2};
  check(finishCalibrationSave(status,2,true)&&calibrationStorageCode(status.load())==3,
    "matching completed window publishes a successful save");
  status.store(8);check(!finishCalibrationSave(status,2,true)&&calibrationStorageCode(status.load())==0,
    "previous window save cannot replace a new calibration request status");
  status.store(10);check(!finishCalibrationSave(status,2,false)&&calibrationStorageCode(status.load())==2,
    "old storage failure cannot overwrite a newer pending window");
  check(finishCalibrationSave(status,10,false)&&calibrationStorageCode(status.load())==-1,
    "current storage failure remains visible in diagnostics");
  std::printf("%d failures\n",failures);return failures?1:0;
}
