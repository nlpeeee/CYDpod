#pragma once
#include <stdint.h>
#include <string.h>
namespace cyd {
// Owned by BtAppT. Transport calls never restart/release the Bluetooth stack.
struct PairingSession {
  enum Phase:uint8_t { Normal, Disconnecting, Scanning, Selecting, Cancelling, Failed };
  Phase phase=Normal;
  bool pairing=false,dropSent=false,cancelSent=false,scanSent=false,connectSent=false;
  uint8_t former[6]{},target[6]{};
  uint32_t deadline=0;
  static bool any(const uint8_t* p){const uint8_t zero[6]{};return memcmp(p,zero,6)!=0;}
  void begin(const uint8_t* old,uint32_t now){
    if(!pairing)memcpy(former,old,6);
    pairing=true;phase=Disconnecting;memset(target,0,6);reset(now);
  }
  void select(const uint8_t* peer,uint32_t now){memcpy(target,peer,6);phase=Selecting;reset(now);}
  void cancel(uint32_t now){memcpy(target,former,6);phase=Cancelling;reset(now);}
  void reset(uint32_t now){dropSent=cancelSent=scanSent=connectSent=false;deadline=now+20000;}
  bool accepts(const uint8_t* peer)const{
    if(phase==Normal)return true;
    if(phase==Scanning)return !any(former)||memcmp(peer,former,6)!=0;
    return (phase==Selecting||phase==Cancelling)&&memcmp(peer,target,6)==0;
  }
  void connected(){phase=Normal;pairing=false;dropSent=cancelSent=scanSent=connectSent=false;}
  void stopped(){scanSent=false;cancelSent=false;}
  template<class T> void drive(uint32_t now,T& t){
    if(phase==Normal||phase==Failed)return;
    if(int32_t(now-deadline)>=0){phase=Failed;t.failed("timeout");return;}
    if(phase==Cancelling&&t.linkConnected()&&memcmp(t.peer(),target,6)==0){connected();return;}
    if(t.linkBusy()){
      if(connectSent&&memcmp(t.peer(),target,6)==0)return;
      if(!dropSent){dropSent=true;if(!t.drop()){phase=Failed;t.failed("disconnect");}}
      return;
    }
    if(t.scanning()){
      if(phase==Scanning)return;
      if(!cancelSent){cancelSent=true;if(!t.stopScan()){phase=Failed;t.failed("cancel inquiry");}}
      return;
    }
    if(phase==Disconnecting||phase==Scanning){
      if(phase!=Scanning)deadline=now+60000;
      phase=Scanning;
      if(!scanSent){scanSent=true;if(!t.startScan()){phase=Failed;t.failed("inquiry");}}
    }else if(!connectSent){
      if(!any(target)){connected();return;}
      connectSent=true;deadline=now+20000;
      if(!t.connect(target)){phase=Failed;t.failed("connect");}
    }
  }
};
}
