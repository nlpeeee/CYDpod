#include "../firmware/cydpod_player/app/bt_volume_sync.h"
#include <cassert>
#include <cstdio>
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
  s.local(30);assert(s.command(400,raw,tl));
  assert(!s.command(1400,raw,tl));assert(s.take(remote,pct)&&pct==100); // Timeout, no retry loop.
  s.local(40);assert(s.command(1500,raw,tl));s.failed();
  assert(s.take(remote,pct)&&pct==100&&!s.command(1600,raw,tl));
  s.connection(false);s.response(20);s.notification(20);
  assert(s.take(remote,pct)&&!remote&&!s.command(1700,raw,tl));
  s.connection(true);assert(!s.absolute);s.notification(20);
  assert(s.take(remote,pct)&&remote&&pct==16);
  s.local(25);assert(s.command(0xfffffff0u,raw,tl));s.response(32);
  s.local(30);assert(!s.command(20,raw,tl)&&s.command(100,raw,tl));
  puts("PASS Bluetooth volume: reports, mute, endpoints, quantization, coalescing, no echoes, timeout, disconnect, reconnect, clock wrap");
}
