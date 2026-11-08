#include "../firmware/cydpod_player/app/bt_volume_sync.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){
  using cyd::BtVolumeSync;
  for(unsigned p=0;p<=100;++p)assert(BtVolumeSync::percent(BtVolumeSync::raw(uint8_t(p)))==p);
  BtVolumeSync s;bool remote=false;uint8_t pct=0,raw=0,tl=0;
  s.notification(100);assert(!s.absolute);
  s.connection(true);s.local(50);assert(!s.command(0,raw,tl));
  s.notification(128);assert(!s.absolute);
  s.notification(64);assert(s.take(remote,pct)&&remote&&pct==50);
  assert(!s.command(0,raw,tl)); // Reports never echo a set-volume command.
  s.local(60);assert(s.command(1,raw,tl)&&raw==76&&tl==3);
  s.local(70);s.local(80);assert(!s.command(200,raw,tl));
  s.response(76);assert(s.take(remote,pct)&&pct==80); // Older response cannot undo the drag.
  assert(s.command(201,raw,tl)&&raw==102&&tl==4);
  s.response(100);assert(s.take(remote,pct)&&pct==79); // Honour receiver quantization.
  s.notification(0);assert(s.take(remote,pct)&&pct==0);
  s.notification(127);assert(s.take(remote,pct)&&pct==100);
  assert(!s.command(300,raw,tl));
  s.local(30);assert(s.command(400,raw,tl));assert(s.take(remote,pct)&&pct==30);
  assert(!s.command(1400,raw,tl));assert(s.take(remote,pct)&&pct==30); // Timeout keeps the asked level, no retry loop.
  assert(!s.command(1500,raw,tl));
  s.response(BtVolumeSync::raw(28));assert(s.take(remote,pct)&&pct==28&&s.lateResponses==1); // A late reply is still the truth.
  s.notification(127);assert(s.take(remote,pct)&&pct==100);
  s.local(40);assert(s.command(1600,raw,tl));s.failed();
  assert(s.take(remote,pct)&&pct==100&&!s.command(1700,raw,tl));
  s.connection(false);s.response(20);s.notification(20);
  assert(s.take(remote,pct)&&!remote&&!s.command(1800,raw,tl));
  s.connection(true);assert(!s.absolute);s.notification(20);
  assert(s.take(remote,pct)&&remote&&pct==16);
  s.local(25);assert(s.command(0xfffffff0u,raw,tl));s.response(32);
  s.local(30);assert(!s.command(20,raw,tl)&&s.command(100,raw,tl));
  // No report from the speaker (stock Bluedroid, or it never answers): after
  // kReportWaitMs a bounded handshake sets CYDpod's level, including mute,
  // without requiring a physical speaker press.
  for(uint8_t initial:{uint8_t(0),uint8_t(40),uint8_t(100)}){
    BtVolumeSync start;start.connection(true);
    assert(start.capabilities(true,initial));
    assert(!start.capabilities(true,90));
    assert(start.take(remote,pct)&&!remote);
    assert(!start.command(0,raw,tl)&&!start.command(999,raw,tl)); // Waiting for the speaker's report.
    assert(start.command(1000,raw,tl)&&raw==BtVolumeSync::raw(initial)&&start.reportWaits==1);
    start.notification(127);assert(!start.absolute); // Old registration state cannot undo the requested initial level.
    start.response(raw);assert(start.take(remote,pct)&&remote&&pct==initial);
    start.connection(true);assert(start.absolute); // Duplicate connected event is harmless.
    assert(!start.command(1100,raw,tl));
    start.connection(false);start.connection(true);
    assert(start.capabilities(true,25)&&!start.command(1200,raw,tl)&&start.command(2200,raw,tl));
    start.local(60);start.response(32);
    assert(start.take(remote,pct)&&remote&&pct==60);
    assert(start.command(2300,raw,tl)&&raw==76);
    start.response(76);assert(start.take(remote,pct)&&pct==60);
  }
  // The speaker's own level on connect is adopted and nothing is sent.
  BtVolumeSync rep;rep.connection(true);
  assert(rep.capabilities(true,25)&&!rep.command(0,raw,tl));
  assert(rep.report(BtVolumeSync::raw(62))&&rep.take(remote,pct)&&remote&&pct==62);
  assert(!rep.command(2000,raw,tl)&&rep.sends==0&&rep.reportWaits==0);
  assert(!rep.report(BtVolumeSync::raw(62))&&rep.reports==2); // Re-arm replies are plain reports.
  rep.notification(BtVolumeSync::raw(31));assert(rep.take(remote,pct)&&pct==31);
  rep.local(40);assert(rep.command(2100,raw,tl));rep.report(BtVolumeSync::raw(31));
  assert(rep.take(remote,pct)&&pct==40); // A report cannot undo a slider move in flight.
  // A report before the volume link is ignored here (bt.cpp replays it on link).
  BtVolumeSync early;assert(!early.report(64)&&early.reports==0);
  // The owner moves the slider while CYDpod waits: the owner's level is sent.
  BtVolumeSync moved;moved.connection(true);
  assert(moved.capabilities(true,25)&&!moved.command(0,raw,tl));
  moved.local(70);assert(moved.command(10,raw,tl)&&raw==BtVolumeSync::raw(70));
  assert(!moved.report(BtVolumeSync::raw(62))); // That command's reply completes the handshake.
  moved.response(BtVolumeSync::raw(70));assert(moved.take(remote,pct)&&remote&&pct==70);
  // A speaker button before any report: its level is adopted.
  BtVolumeSync button;button.connection(true);
  assert(button.capabilities(true,25)&&!button.command(0,raw,tl));
  button.notification(BtVolumeSync::raw(44));
  assert(button.absolute&&button.take(remote,pct)&&pct==44&&!button.command(2000,raw,tl));
  BtVolumeSync fallback;fallback.connection(true);
  assert(!fallback.capabilities(false,40)&&!fallback.command(0,raw,tl));
  fallback.connection(false);fallback.connection(true);
  // A speaker that advertises volume support but never answers: the handshake
  // is retried twice, then CYDpod hands volume to the speaker anyway instead of
  // stacking its own gain on top.
  assert(fallback.capabilities(true,40)&&!fallback.command(0,raw,tl)&&fallback.command(1000,raw,tl)&&raw==BtVolumeSync::raw(40));
  assert(fallback.command(2000,raw,tl)&&raw==BtVolumeSync::raw(40)&&fallback.retries==1);
  assert(fallback.command(3000,raw,tl)&&fallback.retries==2&&fallback.initializing);
  assert(!fallback.command(4000,raw,tl)&&!fallback.initializing&&fallback.absolute);
  assert(fallback.take(remote,pct)&&remote&&pct==40);
  assert(!fallback.capabilities(true,50)&&!fallback.command(5000,raw,tl));
  fallback.notification(64);assert(fallback.take(remote,pct)&&remote&&pct==50);
  // A slow first reply (after a retry) still completes the handshake.
  fallback.connection(false);fallback.connection(true);
  assert(fallback.capabilities(true,40)&&!fallback.command(0,raw,tl)&&fallback.command(1000,raw,tl)&&fallback.command(2000,raw,tl));
  fallback.response(BtVolumeSync::raw(35));
  assert(fallback.take(remote,pct)&&remote&&pct==35&&!fallback.initializing&&!fallback.command(3000,raw,tl));
  // A slider move during the retries is what gets sent next.
  fallback.connection(false);fallback.connection(true);
  assert(fallback.capabilities(true,40)&&!fallback.command(0,raw,tl)&&fallback.command(1000,raw,tl));
  fallback.local(70);assert(fallback.command(2000,raw,tl)&&raw==BtVolumeSync::raw(70));
  fallback.connection(false);fallback.connection(true);
  assert(fallback.capabilities(true,40)&&!fallback.command(0,raw,tl)&&fallback.command(1000,raw,tl));fallback.failed();
  assert(fallback.take(remote,pct)&&!remote&&!fallback.command(3000,raw,tl));
  // Replies to nothing, or on a dropped link, are ignored.
  BtVolumeSync quiet;quiet.connection(true);quiet.response(64);assert(!quiet.absolute&&quiet.responses==0);
  puts("PASS initial Bluetooth volume: speaker report adopted, report vs slider, early report, button before report, no report fallback, confirmed handshake, mute, reconnect, duplicate events, pending slider, unsupported, retried handshake, unanswered handover, slow reply and failure fallback");
  puts("PASS Bluetooth volume: reports, mute, endpoints, quantization, coalescing, no echoes, timeout, disconnect, reconnect, clock wrap");
}
