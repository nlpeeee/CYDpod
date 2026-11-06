#include "../firmware/cydpod_player/app/bt_stream_policy.h"
#include "../firmware/cydpod_player/app/bt_reconnect_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
struct Clock { uint32_t now = 0; uint32_t nowMs() const { return now; } };
struct Sender {
  unsigned starts = 0, reconciles = 0;
  uint32_t epoch = 0;
  bool accept = true;
  template<class Command> bool request(Command cmd, uint32_t e, bool reconcile) {
    if (cmd != Command::START) std::abort();
    ++starts; reconciles += reconcile; epoch = e; return accept;
  }
};
using P = cyd::BtStreamPolicy<Clock, Sender>;
struct Fixture {
  Clock clock; Sender sender; P p{clock, sender};
  Fixture() { p.onLinkUp(7); }
  void play(uint32_t serial = 1) { p.onPlayIntent(serial, true, true); }
  void ack(P::Ack status = P::Ack::OK, uint32_t epoch = 7) { p.onMediaAck(P::Command::START, status, epoch); }
  void suspended() { p.onMediaState(P::Media::STARTED); p.onAudioState(P::Audio::SUSPEND); }
};
static void check(bool ok, const char* name) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) std::exit(1);
}
int main() {
  { Fixture f; f.suspended(); f.play(); f.ack();
    bool waits = f.p.phase() == P::Phase::Waiting;
    f.p.onAudioState(P::Audio::STARTED);
    check(waits && f.sender.starts == 1 && f.p.phase() == P::Phase::Done, "peer takeover then Play sends one START; ACK plus audio completes"); }
  { Fixture f; f.play(); f.play(2); f.ack(); f.p.onAudioState(P::Audio::STARTED); f.play(2); f.p.tick(10000);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Done, "two Plays in flight coalesce and consumed serial cannot replay"); }
  for (auto status : {P::Ack::FAILURE, P::Ack::BUSY}) {
    Fixture f; f.play(); f.ack(status); f.p.tick(1); f.ack(status); f.p.tick(10000);
    check(f.sender.starts == 2 && f.p.phase() == P::Phase::Failed, "failure/busy retries once then stops");
  }
  { Fixture f; f.sender.accept = false; f.play(); f.p.tick(1); f.p.tick(2);
    check(f.sender.starts == 2 && f.p.phase() == P::Phase::Failed, "synchronous sender failure is bounded"); }
  { Fixture f; f.play(); f.clock.now = P::ACK_DEADLINE_MS; f.p.tick(f.clock.now); f.ack(); f.p.tick(9000);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Failed, "missing ACK deadline fails without retry"); }
  { Fixture f; f.play(); f.ack(); f.clock.now = P::ACK_DEADLINE_MS; f.p.onAudioState(P::Audio::STARTED);
    check(f.p.phase() == P::Phase::Failed, "ACK without timely audio STARTED fails"); }
  { Fixture f; f.play(); f.clock.now = P::ACK_DEADLINE_MS; f.ack();
    check(f.p.phase() == P::Phase::Failed, "late ACK before tick cannot revive request"); }
  { Fixture f; cyd::ReconnectPolicy reconnect; reconnect.linkUp();
    f.play(); f.p.onLinkDown(8); reconnect.linkDown(0, false, true, false); f.ack(); f.play(2); f.p.tick(10000);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Cancelled && reconnect.mode == cyd::ReconnectPolicy::HeldPeer,
          "link drop cancels; Play cannot page or release HeldPeer"); }
  { Fixture f; f.play(); f.p.onLinkDown(8); f.p.onLinkUp(9); f.play(2); f.ack(P::Ack::OK, 7); f.p.onAudioState(P::Audio::STARTED);
    bool ignored = f.p.phase() == P::Phase::Waiting;
    f.ack(P::Ack::OK, 9);
    check(ignored && f.p.phase() == P::Phase::Done && f.sender.epoch == 9, "stale epoch ACK ignored after new link"); }
  { Fixture f; f.play(); f.p.onRouteChange(false); f.p.onPlayIntent(2, false, true); f.ack(); f.p.tick(10000);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Cancelled, "wired route cancels and never starts"); }
  { Fixture f; f.play(); f.p.onPause(); f.ack(); f.p.tick(10000);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Cancelled, "pause cancels"); }
  { Fixture f; f.play(); f.p.onNewTrack(); f.ack(); f.p.tick(10000);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Cancelled, "new track cancels"); }
  { Fixture f; f.play(); f.p.onPairing(); f.play(2); f.ack(); f.p.tick(10000); f.p.onPairing(false); f.play(2);
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Cancelled, "pairing cancels and consumed intent cannot start after pairing"); }
  { Fixture f; f.p.onAudioState(P::Audio::STARTED); f.p.onMediaState(P::Media::STARTED); f.play();
    check(f.sender.starts == 0 && f.p.phase() == P::Phase::Done, "already STARTED avoids duplicate"); }
  { Fixture f; f.p.onPlayIntent(1, true, false); f.p.tick(10000); f.p.onRouteChange(true); f.p.tick(20000);
    check(f.sender.starts == 0, "boot paused and route return never send"); }
  { Clock c; Sender s; P p(c, s, 42); p.onLinkUp(1); p.onPlayIntent(42, true, true); p.tick(10000);
    check(s.starts == 0, "boot seeds current intent without reclaim"); }
  { Fixture f; f.suspended(); f.p.tick(10000); f.p.tick(20000);
    bool quiet = f.sender.starts == 0; f.play();
    check(quiet && f.sender.starts == 1 && f.sender.reconciles == 1, "STARTED with SUSPEND reconciles only on explicit Play"); }
  { Fixture f; f.p.onAudioState(P::Audio::STARTED); f.play(); f.ack();
    check(f.sender.starts == 1 && f.p.phase() == P::Phase::Done, "stale media with STARTED audio reconciles"); }
  { Fixture f; f.clock.now = UINT32_MAX - 1000; f.play(); f.clock.now += P::ACK_DEADLINE_MS - 1; f.p.tick(f.clock.now);
    bool waiting = f.p.phase() == P::Phase::Waiting; ++f.clock.now; f.p.tick(f.clock.now);
    check(waiting && f.p.phase() == P::Phase::Failed, "deadline handles millis rollover"); }
}
