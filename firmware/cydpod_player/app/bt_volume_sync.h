#pragma once
#include <stdint.h>

namespace cyd {
// Called under the Bluetooth volume lock. No allocation, polling commands or
// notification echoes. A slider drag coalesces behind one outstanding command.
//
// The speaker owns absolute volume. AVRCP's only read of it is the INTERIM
// reply to a volume-change registration, which stock Bluedroid drops; the
// CYDpod Bluedroid build forwards it (tools/bluedroid/patches). On connect
// CYDpod adopts that report instead of pushing its own level. Without one
// (stock libraries, or a speaker that never answers) it falls back to setting
// its own level after kReportWaitMs. Beyond that, the level is learned from
// CHANGED notifications and command replies, and the sync never discards them:
//  - a command reply that arrives after the 1 s timeout still updates the level;
//  - a timeout keeps the level the owner asked for (the command was accepted),
//    instead of snapping the slider back to an older report;
//  - the initial handshake is retried, and a speaker that advertises volume
//    notifications is treated as absolute even if it never answers it, so
//    CYDpod does not stack its own gain on top of the speaker's.
struct BtVolumeSync {
  static const uint32_t kTimeoutMs=1000;
  static const uint8_t kInitTries=3;
  static const uint32_t kReportWaitMs=1000;
  bool linked=false,absolute=false,dirty=false,pending=false,inFlight=false;
  uint8_t reported=0,value=0,wanted=0,label=2,sent=0,initTries=0;
  uint32_t sentAt=0;
  bool sentBefore=false,capabilitiesSeen=false,initializing=false;
  bool awaitingReport=false,waitStarted=false;
  uint32_t waitAt=0;
  // Diagnostics for `stat: vol` (never reset by a link change).
  uint32_t notifications=0,responses=0,lateResponses=0,timeouts=0,retries=0,sends=0,reports=0,reportWaits=0;
  static uint8_t percent(uint8_t raw){return uint8_t((unsigned(raw)*100+63)/127);}
  static uint8_t raw(uint8_t pct){return uint8_t((unsigned(pct>100?100:pct)*127+50)/100);}
  void connection(bool up){
    if(linked==up)return;
    linked=up;absolute=false;pending=inFlight=sentBefore=false;dirty=true;
    capabilitiesSeen=initializing=awaitingReport=waitStarted=false;reported=value=wanted=sent=initTries=0;
  }
  bool capabilities(bool supported,uint8_t pct){
    if(!linked||capabilitiesSeen)return false;
    capabilitiesSeen=true;
    if(!supported||absolute)return false;
    // Wait briefly for the speaker's own level (report()). CHANGE_NOTIFY may
    // arrive only after a physical button press, so without a report CYDpod
    // establishes one confirmed setting of its own instead of waiting forever.
    initializing=awaitingReport=true;waitStarted=false;initTries=0;value=wanted=raw(pct);pending=false;dirty=true;return true;
  }
  // The speaker's current level, read when the volume notification is
  // registered. Completes the connect handshake by adopting it, unless the
  // owner moved the slider meanwhile (that is then sent). Returns true when
  // it completed the handshake.
  bool report(uint8_t rawVolume){
    if(!linked||rawVolume>127)return false;
    ++reports;reported=rawVolume;
    if(initializing&&inFlight)return false; // That command's reply completes the handshake.
    const bool first=initializing;
    initializing=awaitingReport=false;absolute=true;
    if(!pending&&!inFlight)value=reported;
    dirty=true;return first;
  }
  void notification(uint8_t rawVolume){
    if(!linked||rawVolume>127)return;
    ++notifications;
    reported=rawVolume;
    if(initializing){
      if(!awaitingReport)return; // The initial command response completes the handoff.
      initializing=awaitingReport=false; // A button press before any report: the speaker's level.
    }
    absolute=true;
    if(!pending&&!inFlight){value=reported;dirty=true;}
  }
  void local(uint8_t pct){
    if(!absolute&&!initializing)return;
    value=wanted=raw(pct);pending=true;dirty=true;
  }
  // The receiver's actual setting. A reply after the timeout is still the
  // truth about the speaker, so it is applied rather than dropped.
  void response(uint8_t rawVolume){
    if(!linked||rawVolume>127||!sentBefore)return;
    ++responses;if(!inFlight)++lateResponses;
    inFlight=false;initializing=false;absolute=true;reported=rawVolume;
    if(!pending)value=reported;
    dirty=true;
  }
  bool command(uint32_t now,uint8_t& rawVolume,uint8_t& transaction){
    if(awaitingReport&&linked){
      if(!waitStarted){waitStarted=true;waitAt=now;}
      else if(uint32_t(now-waitAt)>=kReportWaitMs){awaitingReport=false;++reportWaits;pending=true;} // wanted holds CYDpod's level
    }
    if(inFlight&&uint32_t(now-sentAt)>=kTimeoutMs){
      inFlight=false;++timeouts;
      if(initializing&&initTries<kInitTries){++initTries;++retries;if(!pending){wanted=sent;pending=true;}}
      else{
        // Accepted but unanswered: assume it took effect. A later reply or
        // notification corrects this; reverting would show a level the
        // speaker has probably already left.
        if(initializing){initializing=false;absolute=true;}
        reported=sent;
        if(!pending)value=reported;
        dirty=true;
      }
    }
    if(!linked||(!absolute&&!initializing)||!pending||inFlight||(sentBefore&&uint32_t(now-sentAt)<100))return false;
    pending=false;
    if(absolute&&wanted==reported){value=reported;dirty=true;return false;}
    if(initializing){awaitingReport=false;if(!initTries)initTries=1;}
    rawVolume=sent=wanted;inFlight=true;sentBefore=true;sentAt=now;++sends;
    // Labels 0/1/2 belong to the library's other AVRCP requests.
    label=label==15?3:uint8_t(label+1);transaction=label;return true;
  }
  void failed(){inFlight=initializing=awaitingReport=false;pending=false;value=reported;dirty=true;}
  bool take(bool& remote,uint8_t& pct){
    if(!dirty)return false;
    dirty=false;remote=absolute;pct=percent(value);return true;
  }
};
}
