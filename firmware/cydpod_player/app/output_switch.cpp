#ifndef CYD_UPLOADER
#include "output_switch.h"
#include "audio.h"
#include "speaker.h"
#include "storage.h"
#include "catalog.h"
#include "ui.h"

namespace {
struct SwitchOps {
  bool wasPlaying = false;
  uint8_t activeRoute = speaker_route();
  bool parked = false;
  void failed(const char* stage) { Serial.printf("output-switch: failed stage=%s\n", stage); }
  bool pending() {
    const AudioCommands c = audio_commands();
    return c.track >= 0 || c.seekMs >= 0 || c.opening || c.seeking || !audio_startupComplete();
  }
  bool capture() {
    wasPlaying = audio_isPlaying();
    audio_setPlaying(false);
    // Pending track/seek commands must settle before claiming an exact resume point.
    const uint32_t at = millis();
    while (pending() && millis()-at < 5000) delay(10);
    if (pending()) return false; // leave the previous resume identity untouched
    g_settings.lastTrack = audio_currentTrack();
    g_settings.lastPath = storage_trackPath(g_settings.lastTrack);
    g_settings.lastPosMs = audio_positionMs();
    return true;
  }
  bool park() {
    if (pending()) return false;
    parked = speaker_parkForRestart(); // retain old task/work for save rollback
    return parked;
  }
  bool save(uint8_t next) {
    // Flush pending UI preferences while the old route is still selected.
    if (!settings_save()) { failed("preferences"); return false; }
    return settings_saveOutputResume(next);
  }
  void restore(uint8_t old) {
    (void)old;
    if (parked) speaker_restoreAfterRestartPark(activeRoute);
    audio_setPlaying(wasPlaying);
  }
  void restart(uint8_t next) {
    g_settings.outRoute = next;
    // Search is the only incremental SD writer; all other UI writes close
    // synchronously. Decode reads only. Finish the pending search write/close.
    storage_searchCancel();
    ui_toast("Restarting...");
    Serial.println("cmd: output saved; restarting paused at resume point");
    Serial.flush();
    delay(350);
    ESP.restart();
  }
};
}
bool output_switch(uint8_t next) {
  SwitchOps ops;
  const bool ok = output_switch_run(g_settings.outRoute, next, ops);
  if (!ok) ui_toast("Unable to change output. Try again.");
  return ok;
}
#endif
