#pragma once
#include <stdint.h>

namespace cyd {
// Called under the Bluetooth volume lock. No allocation, polling commands or
// notification echoes. A slider drag coalesces behind one outstanding command.
struct BtVolumeSync {
  bool linked=false,absolute=false,dirty=false,pending=false,inFlight=false;
  uint8_t reported=0,value=0,wanted=0,label=2;
  uint32_t sentAt=0;
  bool sentBefore=false;
  static uint8_t percent(uint8_t raw){return uint8_t((unsigned(raw)*100+63)/127);}
  static uint8_t raw(uint8_t pct){return uint8_t((unsigned(pct>100?100:pct)*127+50)/100);}
  void connection(bool up){
    linked=up;absolute=false;pending=inFlight=sentBefore=false;dirty=true;
  }
  void notification(uint8_t rawVolume){
    if(!linked||rawVolume>127)return;
    reported=rawVolume;absolute=true;
    if(!pending&&!inFlight){value=reported;dirty=true;}
  }
  void local(uint8_t pct){
    if(!absolute)return;
    value=wanted=raw(pct);pending=true;dirty=true;
  }
  void response(uint8_t rawVolume){
    if(!linked||!absolute||!inFlight||rawVolume>127)return;
    inFlight=false;reported=rawVolume;
    if(!pending){value=reported;dirty=true;}
  }
  bool command(uint32_t now,uint8_t& rawVolume,uint8_t& transaction){
    if(inFlight&&uint32_t(now-sentAt)>=1000){
      inFlight=false;
      if(!pending){value=reported;dirty=true;}
    }
    if(!linked||!absolute||!pending||inFlight||(sentBefore&&uint32_t(now-sentAt)<100))return false;
    pending=false;
    if(wanted==reported){value=reported;dirty=true;return false;}
    rawVolume=wanted;inFlight=true;sentBefore=true;sentAt=now;
    // Labels 0/1/2 belong to the library's other AVRCP requests.
    label=label==15?3:uint8_t(label+1);transaction=label;return true;
  }
  void failed(){inFlight=false;pending=false;value=reported;dirty=true;}
  bool take(bool& remote,uint8_t& pct){
    if(!dirty)return false;
    dirty=false;remote=absolute;pct=percent(value);return true;
  }
};
}
