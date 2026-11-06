#pragma once
#include <stdint.h>
namespace cyd {
// Single-task owner. Clock::nowMs() returns uint32_t milliseconds.
// Sender::request(Command, epoch, reconcile) returns whether START was accepted.
// On BtAppT it must set library media STARTING before sending START (including
// stale STARTED/SUSPEND reconciliation), never expose IDLE to the heartbeat.
// No reconnect, disconnect, allocation, or automatic stream reclaim exists here.
template<class Clock, class Sender> class BtStreamPolicy {
 public:
  enum class Audio : uint8_t { SUSPEND, STARTED };
  enum class Media : uint8_t { IDLE, STARTING, STARTED, STOPPING };
  enum class Command : uint8_t { CHECK_SRC_RDY, START };
  enum class Ack : uint8_t { OK, BUSY, FAILURE };
  enum class Phase : uint8_t { Idle, Waiting, Retry, Done, Failed, Cancelled };
  static constexpr uint32_t ACK_DEADLINE_MS = 3000;
  static constexpr uint8_t MAX_RETRIES = 1;

  BtStreamPolicy(Clock& clock, Sender& sender, uint32_t initialSerial = 0)
      : clock_(clock), sender_(sender), serial_(initialSerial) {}
  Phase phase() const { return phase_; }
  uint8_t attempts() const { return attempts_; }
  bool inFlight() const { return phase_ == Phase::Waiting || phase_ == Phase::Retry; }
  void onLinkUp(uint32_t epoch) {
    cancel(); epoch_ = epoch; linked_ = true;
    audio_ = Audio::SUSPEND; media_ = Media::IDLE;
  }
  void onLinkDown(uint32_t epoch) {
    cancel(); epoch_ = epoch; linked_ = false;
    audio_ = Audio::SUSPEND; media_ = Media::IDLE;
  }
  void onAudioState(Audio state) {
    audio_ = state; complete(); // suspend alone never changes library media
  }
  void onMediaState(Media state) { media_ = state; }
  void onMediaAck(Command cmd, Ack status, uint32_t epoch) {
    if (!linked_ || epoch != epoch_ || phase_ != Phase::Waiting || cmd != Command::START) return;
    // A late ACK cannot revive an expired request, even before the next tick.
    if (reached(clock_.nowMs(), deadline_)) { phase_ = Phase::Failed; return; }
    if (status == Ack::OK) { acked_ = true; complete(); }
    else if (!acked_) {
      phase_ = attempts_ <= MAX_RETRIES ? Phase::Retry : Phase::Failed;
    }
  }
  void onPlayIntent(uint32_t serial, bool routeIsBt, bool playing) {
    routeBt_ = routeIsBt; playing_ = playing;
    if (!routeBt_ || !playing_) cancel();
    if (serial == serial_) return;
    serial_ = serial; // consume even rejected/coalesced intents; never replay them
    if (inFlight() || !linked_ || !routeBt_ || !playing_ || pairing_) return;
    attempts_ = 0;
    if (audio_ == Audio::STARTED && media_ == Media::STARTED) { phase_ = Phase::Done; return; }
    start(clock_.nowMs());
  }
  void onPause() { playing_ = false; cancel(); }
  void onNewTrack() { cancel(); }
  void onRouteChange(bool routeIsBt) { routeBt_ = routeIsBt; cancel(); }
  void onPairing(bool active = true) { pairing_ = active; cancel(); }
  void tick(uint32_t nowMs) {
    if (phase_ == Phase::Waiting && reached(nowMs, deadline_)) phase_ = Phase::Failed;
    else if (phase_ == Phase::Retry) {
      if (linked_ && routeBt_ && playing_ && !pairing_) start(nowMs);
      else cancel();
    }
  }
 private:
  static bool reached(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }
  void cancel() { if (inFlight()) phase_ = Phase::Cancelled; acked_ = false; }
  void complete() {
    if (phase_ != Phase::Waiting) return;
    if (reached(clock_.nowMs(), deadline_)) phase_ = Phase::Failed;
    else if (acked_ && audio_ == Audio::STARTED) phase_ = Phase::Done;
  }
  void start(uint32_t now) {
    const bool reconcile = media_ != Media::IDLE;
    ++attempts_; acked_ = false; deadline_ = now + ACK_DEADLINE_MS;
    phase_ = Phase::Waiting;
    // Set our mirror first too; the injected sender must not call back inline.
    media_ = Media::STARTING;
    if (!sender_.request(Command::START, epoch_, reconcile))
      phase_ = attempts_ <= MAX_RETRIES ? Phase::Retry : Phase::Failed;
  }
  Clock& clock_;
  Sender& sender_;
  uint32_t serial_ = 0, epoch_ = 0, deadline_ = 0;
  uint8_t attempts_ = 0;
  bool linked_ = false, routeBt_ = false, playing_ = false, pairing_ = false, acked_ = false;
  Audio audio_ = Audio::SUSPEND;
  Media media_ = Media::IDLE;
  Phase phase_ = Phase::Idle;
};
}
