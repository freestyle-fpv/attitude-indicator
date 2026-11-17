#pragma once
#include "installation.h"

namespace instrument {
enum class SettingsPage : uint8_t { Closed, Main, Direction };
struct DisplayOptions {
  UsbSide side=UsbSide::Left;
  bool fps=true;
  uint8_t encode()const{return unsigned(side)|(fps?4:0);}
  static DisplayOptions decode(uint8_t value){
    DisplayOptions options;
    if(value<8){options.side=UsbSide(value&3);options.fps=(value&4)!=0;}
    return options;
  }
};
class SettingsMenu {
 public:
  static constexpr int RowX=98,RowWidth=270;
  static AirspeedRect row(SettingsPage page,int index){
    return page==SettingsPage::Direction?AirspeedRect{RowX,136+58*index,RowWidth,52}:
                                         AirspeedRect{RowX,174+80*index,RowWidth,66};
  }
  static const char* sideLabel(UsbSide side){
    const char* const names[]={"LEFT","TOP","RIGHT","BOTTOM"};return names[unsigned(side)];
  }
  SettingsPage page=SettingsPage::Closed;
  DisplayOptions options;
  void click(){
    page=page==SettingsPage::Main?SettingsPage::Closed:SettingsPage::Main;
  }
  bool tap(int x,int y){
    if(page==SettingsPage::Closed||x<RowX||x>=RowX+RowWidth)return false;
    for(int i=0;i<(page==SettingsPage::Main?2:4);++i){
      const auto bounds=row(page,i);
      if(y<bounds.y||y>=bounds.y+bounds.height)continue;
      if(page==SettingsPage::Direction){options.side=UsbSide(i);page=SettingsPage::Main;}
      else if(i==0)page=SettingsPage::Direction;
      else options.fps=!options.fps;
      return true;
    }
    return false;
  }
};
}
