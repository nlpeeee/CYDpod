#include "../firmware/cydpod_player/app/pairing_session.h"
#include <iostream>
#include <stdexcept>
using cyd::PairingSession;
struct Radio{
 bool busy=false,scan=false,starting=false,fail=false;uint8_t addr[6]{};int drops=0,stops=0,scans=0,connects=0,errors=0;
 bool linkBusy(){return busy;}bool linkConnected(){return busy;}bool scanning(){return scan;}bool inquiryStarting(){return starting;}const uint8_t* peer(){return addr;}
 bool drop(){++drops;return !fail;}bool stopScan(){++stops;return !fail;}
 bool startScan(){++scans;return !fail;}bool connect(const uint8_t* p){++connects;memcpy(addr,p,6);return !fail;}
 void failed(const char*){++errors;}
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
 std::cout<<"PASS live pairing: 20 cycles, disconnect/inquiry start+stop acknowledgment, peer filtering, cancellation, timeout and API failure; no restart transport exists\n";return 0;
 }catch(...){std::cerr<<"FAIL live pairing\n";return 1;}}
