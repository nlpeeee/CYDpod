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
  // No initial notification: connection capabilities must trigger a bounded
  // handshake, including mute, without requiring a physical speaker press.
  for(uint8_t initial:{uint8_t(0),uint8_t(40),uint8_t(100)}){
    BtVolumeSync start;start.connection(true);
    assert(start.capabilities(true,initial));
    assert(!start.capabilities(true,90));
    assert(start.take(remote,pct)&&!remote);
    assert(start.command(0,raw,tl)&&raw==BtVolumeSync::raw(initial));
    start.notification(127);assert(!start.absolute); // Old registration state cannot undo the requested initial level.
    start.response(raw);assert(start.take(remote,pct)&&remote&&pct==initial);
    start.connection(true);assert(start.absolute); // Duplicate connected event is harmless.
    assert(!start.command(100,raw,tl));
    start.connection(false);start.connection(true);
    assert(start.capabilities(true,25)&&start.command(200,raw,tl));
    start.local(60);start.response(32);
    assert(start.take(remote,pct)&&remote&&pct==60);
    assert(start.command(300,raw,tl)&&raw==76);
    start.response(76);assert(start.take(remote,pct)&&pct==60);
  }
  BtVolumeSync fallback;fallback.connection(true);
  assert(!fallback.capabilities(false,40)&&!fallback.command(0,raw,tl));
  fallback.connection(false);fallback.connection(true);
  assert(fallback.capabilities(true,40)&&fallback.command(0,raw,tl));
  assert(!fallback.command(1000,raw,tl));
  assert(fallback.take(remote,pct)&&!remote&&!fallback.initializing);
  assert(!fallback.capabilities(true,50)&&!fallback.command(2000,raw,tl));
  fallback.notification(64);assert(fallback.take(remote,pct)&&remote&&pct==50);
  fallback.connection(false);fallback.connection(true);
  assert(fallback.capabilities(true,40)&&fallback.command(0,raw,tl));fallback.failed();
  assert(fallback.take(remote,pct)&&!remote&&!fallback.command(2000,raw,tl));
  puts("PASS initial Bluetooth volume: no initial report, confirmed handshake, mute, reconnect, duplicate events, pending slider, unsupported, timeout and failure fallback");
  puts("PASS Bluetooth volume: reports, mute, endpoints, quantization, coalescing, no echoes, timeout, disconnect, reconnect, clock wrap");
}
