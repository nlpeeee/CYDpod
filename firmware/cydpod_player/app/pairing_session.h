#pragma once
#include <stdint.h>
#include <string.h>
namespace cyd {
// Owned by BtAppT. Transport calls never restart/release the Bluetooth stack.
struct PairingSession {
  enum Phase:uint8_t { Normal, Disconnecting, Scanning, Selecting, Cancelling, Failed };
  Phase phase=Normal;
  // background: a page to the saved speaker (see page()); it never counts as pairing.
  bool pairing=false,background=false,dropSent=false,cancelSent=false,scanSent=false,connectSent=false;
  uint8_t former[6]{},target[6]{};
  uint32_t deadline=0;
  static bool any(const uint8_t* p){const uint8_t zero[6]{};return memcmp(p,zero,6)!=0;}
  void begin(const uint8_t* old,uint32_t now){
    if(!pairing)memcpy(former,old,6);
    pairing=true;background=false;phase=Disconnecting;memset(target,0,6);reset(now);
  }
  void select(const uint8_t* peer,uint32_t now){memcpy(target,peer,6);phase=Selecting;background=false;reset(now);}
  // The player reaching for its saved speaker unprompted. Only from an idle
  // session; it never drops a link, and ends back in Normal whatever happens.
  bool page(const uint8_t* peer,uint32_t now){
    if(pairing||(phase!=Normal&&phase!=Failed))return false;
    memcpy(target,peer,6);phase=Selecting;background=true;reset(now);return true;
  }
  // Only meaningful for a session that is running (or just failed): a Cancel that
  // arrives after the new device connected must not drop it.
  void cancel(uint32_t now){
    if(!pairing&&phase!=Failed)return;
    pairing=true;background=false;memcpy(target,former,6);phase=Cancelling;reset(now);
  }
  void reset(uint32_t now){dropSent=cancelSent=scanSent=connectSent=false;deadline=now+20000;}
  // known: the peer is a device the owner has connected before.
  bool accepts(const uint8_t* peer,bool known=false)const{
    switch(phase){
      case Normal: case Failed: return known;
      case Scanning: return !any(former)||memcmp(peer,former,6)!=0;
      case Selecting: return memcmp(peer,target,6)==0||(background&&known);
      case Cancelling: return memcmp(peer,target,6)==0;
      default: return false;
    }
  }
  bool needsTick()const{return phase!=Normal&&phase!=Failed;}
  void connected(){phase=Normal;pairing=background=false;dropSent=cancelSent=scanSent=connectSent=false;memset(former,0,6);}
  void stopped(){scanSent=false;cancelSent=false;}
  template<class T> void fail(T& t,const char* what){
    if(!background&&!cancelSent&&(t.scanning()||t.inquiryStarting())){
      cancelSent=true;t.stopScan();
    }
    if(background){background=false;phase=Normal;t.pageFailed(what);}
    else{phase=Failed;pairing=false;t.failed(what);}
  }
  template<class T> void drive(uint32_t now,T& t){
    if(phase==Normal||phase==Failed)return;
    if(int32_t(now-deadline)>=0){fail(t,"timeout");return;}
    if(phase==Cancelling&&t.linkConnected()&&memcmp(t.peer(),target,6)==0){connected();return;}
    if(t.linkBusy()){
      if(connectSent&&memcmp(t.peer(),target,6)==0)return;
      if(background){fail(t,"busy");return;}
      if(!dropSent){dropSent=true;if(!t.drop())fail(t,"disconnect");}
      return;
    }
    // Do not select/cancel before the asynchronous inquiry-start acknowledgment.
    if(t.inquiryStarting())return;
    if(t.scanning()){
      if(phase==Scanning)return;
      if(!cancelSent){cancelSent=true;if(!t.stopScan())fail(t,"cancel inquiry");}
      return;
    }
    if(phase==Disconnecting||phase==Scanning){
      if(phase!=Scanning)deadline=now+60000;
      phase=Scanning;
      if(!scanSent){scanSent=true;if(!t.startScan())fail(t,"inquiry");}
    }else if(!connectSent){
      if(!any(target)){connected();return;}
      connectSent=true;deadline=now+20000;
      if(!t.connect(target))fail(t,"connect");
    }
  }
};
}
