#include "../firmware/cydpod_player/app/pairing_session.h"
#include <iostream>
#include <stdexcept>
using cyd::PairingSession;
struct Radio{
 bool busy=false,scan=false,starting=false,fail=false;uint8_t addr[6]{};int drops=0,stops=0,scans=0,connects=0,errors=0,pageErrors=0;
 bool linkBusy(){return busy;}bool linkConnected(){return busy;}bool scanning(){return scan;}bool inquiryStarting(){return starting;}const uint8_t* peer(){return addr;}
 bool drop(){++drops;return !fail;}bool stopScan(){++stops;return !fail;}
 bool startScan(){++scans;return !fail;}bool connect(const uint8_t* p){++connects;memcpy(addr,p,6);return !fail;}
 void failed(const char*){++errors;}void pageFailed(const char*){++pageErrors;}
};
static void ck(bool b){if(!b)throw std::runtime_error("pairing transition");}
int main(){try{const uint8_t old[6]{1},next[6]{2},none[6]{};
 PairingSession repeated;
 for(int cycle=0;cycle<20;++cycle){PairingSession& p=repeated;Radio r;r.busy=true;memcpy(r.addr,old,6);p.begin(old,100);p.drive(101,r);ck(r.drops==1&&r.scans==0&&!p.accepts(old));
  p.drive(102,r);ck(r.drops==1);r.busy=false;p.drive(103,r);ck(r.scans==1&&p.phase==PairingSession::Scanning&&!p.accepts(old)&&p.accepts(next));
  r.scan=true;p.select(next,104);p.drive(105,r);ck(r.stops==1&&r.connects==0);r.scan=false;p.stopped();p.drive(106,r);ck(r.connects==1&&!p.accepts(old)&&p.accepts(next));p.connected();ck(!p.pairing&&p.phase==PairingSession::Normal);
 }
 {PairingSession p;Radio r;p.begin(old,0);r.scan=true;p.cancel(1);p.drive(2,r);ck(r.stops==1);r.scan=false;p.stopped();p.drive(3,r);ck(r.connects==1&&p.accepts(old)&&!p.accepts(next));p.connected();ck(!p.pairing);}
 {PairingSession p;Radio r;p.begin(none,0);p.cancel(1);p.drive(2,r);ck(p.phase==PairingSession::Normal&&r.connects==0);}
 {PairingSession p;Radio r;p.begin(old,0);p.drive(20000,r);ck(p.phase==PairingSession::Failed&&r.errors==1);p.cancel(20001);p.drive(20002,r);ck(r.connects==1);}
 {PairingSession p;Radio r;r.fail=true;p.begin(old,0);p.drive(1,r);ck(p.phase==PairingSession::Failed&&r.errors==1);}
 {PairingSession p;Radio r;r.busy=true;memcpy(r.addr,old,6);p.begin(old,0);p.cancel(1);p.drive(2,r);ck(p.phase==PairingSession::Normal&&r.drops==0&&!p.pairing);}
 {PairingSession p;Radio r;p.begin(old,0);p.drive(1,r);r.scan=true;r.scan=false;p.stopped();p.drive(20001,r);p.drive(60002,r);ck(p.phase==PairingSession::Failed&&r.errors==1);}
 {PairingSession p;Radio r;p.begin(old,0);p.drive(1,r);r.starting=true;p.select(next,2);p.drive(3,r);ck(r.stops==0&&r.connects==0);
  r.starting=false;r.scan=true;p.drive(4,r);ck(r.stops==1&&r.connects==0);r.scan=false;p.stopped();p.drive(5,r);ck(r.connects==1);}
 {PairingSession p;Radio r;r.busy=true;r.fail=true;p.begin(old,0);p.drive(1,r);ck(p.phase==PairingSession::Failed&&r.errors==1);}
 {PairingSession p;Radio r;r.scan=true;r.fail=true;p.select(next,0);p.drive(1,r);ck(p.phase==PairingSession::Failed&&r.errors==1);}
 {PairingSession p;Radio r;r.fail=true;p.select(next,0);p.drive(1,r);ck(p.phase==PairingSession::Failed&&r.errors==1);}
 // CY-114: Failed ends the pairing; Cancel after connected() is a no-op; stranger vs known peers.
 {PairingSession p;Radio r;p.begin(old,0);p.drive(20000,r);ck(p.phase==PairingSession::Failed&&!p.pairing&&r.errors==1);}
 {PairingSession p;Radio r;const uint8_t other[6]{9};p.begin(old,0);p.connected();ck(p.phase==PairingSession::Normal&&!PairingSession::any(p.former));
  r.busy=true;memcpy(r.addr,next,6);p.cancel(5);p.drive(6,r);ck(r.drops==0&&p.phase==PairingSession::Normal&&!p.pairing);
  ck(!p.accepts(other,false)&&p.accepts(other,true));p.begin(old,10);p.drive(20010,r);ck(p.phase==PairingSession::Failed&&!p.accepts(other,false)&&p.accepts(other,true));}
 // CY-114: a background page is not a pairing, never drops a link, accepts known peers, and ends in Normal.
 {PairingSession p;Radio r;const uint8_t car[6]{7},stranger[6]{8};ck(p.page(old,0)&&!p.pairing&&p.background&&p.phase==PairingSession::Selecting);
  p.drive(1,r);ck(r.connects==1&&r.drops==0&&p.accepts(old)&&p.accepts(car,true)&&!p.accepts(stranger,false));
  p.drive(2,r);p.connected();ck(p.phase==PairingSession::Normal&&!p.background);}
 {PairingSession p;Radio r;ck(p.page(old,0));p.drive(20000,r);ck(p.phase==PairingSession::Normal&&r.pageErrors==1&&r.errors==0&&!p.background);}
 {PairingSession p;Radio r;r.busy=true;memcpy(r.addr,next,6);ck(p.page(old,0));p.drive(1,r);ck(r.pageErrors==1&&r.drops==0&&r.connects==0&&p.phase==PairingSession::Normal);}
 {PairingSession p;Radio r;r.fail=true;ck(p.page(old,0));p.drive(1,r);ck(r.pageErrors==1&&r.errors==0&&p.phase==PairingSession::Normal);}
 {PairingSession p;p.begin(old,0);ck(!p.page(old,1));PairingSession q;q.select(next,0);ck(!q.page(old,1)&&!q.accepts(old,true));}
 // CY-118: erased settings (no saved speaker): Pair new while the library's own boot inquiry is running reaches Scanning,
 // takes a list pick and connects.
 {PairingSession p;Radio r;const uint8_t pick[6]{5};r.scan=true;p.begin(none,0);p.drive(1,r);ck(r.stops==1&&r.scans==0&&p.phase==PairingSession::Disconnecting);
  r.scan=false;p.stopped();p.drive(2,r);ck(r.scans==1&&p.phase==PairingSession::Scanning&&p.accepts(pick)&&p.pairing);
  p.select(pick,3);p.drive(4,r);ck(r.connects==1&&p.accepts(pick));p.connected();ck(!p.pairing&&p.phase==PairingSession::Normal&&r.errors==0);}
 std::cout<<"PASS live pairing: 20 cycles, disconnect/inquiry start+stop acknowledgment, peer filtering, cancellation, timeout and API failure; no restart transport exists\n";return 0;
 }catch(...){std::cerr<<"FAIL live pairing\n";return 1;}}
