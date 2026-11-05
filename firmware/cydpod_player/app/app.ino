// ===========================================================================
// CYD Bluetooth MP3 Player -- application (ESP32-2432S028R, 2.8")
// ===========================================================================
// Music from the SD card, played to a Bluetooth speaker or the I2S module, with a touch UI.
// Every piece of hardware behaviour this relies on was
// proven first in stage0/ -- see docs/stage0_results.md for the numbers.
//
//   Display  HSPI      TFT_eSPI, ST7789 or ILI9341 (runtime switch, display.h)
//   SD card  VSPI      music
//   Touch    bit-bang  XPT2046 (touch.h)
//   Audio    Bluetooth A2DP source (bt.h) fed by the decode task (audio.h)
//
// Boot order matters for memory: rendering song titles (~100 KB of font
// metrics) runs BEFORE
// Bluetooth takes its ~80 KB.
//
// This sketch builds TWO firmware images (firmware.h): the player, and with
// CYD_UPLOADER defined, the WiFi uploader. tools/flash.sh builds and flashes
// both; setup() below is shared up to the point where they part ways.

#include <SD.h>
#include <TFT_eSPI.h>

#include "board.h"
#include "settings.h"
#include "display.h"
#include "touch.h"
#include "calibrate.h"
#include "theme.h"
#include "storage.h"
#include "audio.h"
#include "bt.h"
#include "boot_plan.h"
#include <esp32-hal-bt.h>
#include "speaker.h"
#if CYD_WIRED_OUTPUT
#include "wired.h"
#endif
#include "ui.h"
#include "titles.h"
#include "playlist.h"
#include "uploadmode.h"
#include "firmware.h"
#include "catalog.h"
#include "classic_screen.h"
#include "sleep_mode.h"
#include <esp_system.h>

// The loop task (which also runs setup: calibration, title rendering) measured
// 2.8 KB of its default 8 KB stack in use. 5 KB keeps a wide margin and hands
// 3 KB back to a heap Bluetooth leaves short.
SET_LOOP_TASK_STACK_SIZE(5 * 1024);

#ifdef CYD_UPLOADER
static const char* FIRMWARE_LABEL = FIRMWARE_VERSION " uploader";
#else
static const char* FIRMWARE_LABEL = FIRMWARE_VERSION;
static bool bluetoothStarted = false;
static bool bluetoothPlanned = false;
#endif

// ---------------------------------------------------------------------------
// Compile-time cross-checks: board.h vs TFT_eSPI's User_Setup.h
// ---------------------------------------------------------------------------
// Each of these is a mistake with no runtime error -- a dead panel, or an SD
// card that will not mount -- so it is worth stopping the build for.
#ifndef USE_HSPI_PORT
  #error "User_Setup.h is missing '#define USE_HSPI_PORT' -- the display would take VSPI, which the SD card needs. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
#endif
#if CYD_PANEL_DRIVER == CYD_PANEL_ST7789
  #if !defined(ST7789_DRIVER) && !defined(ST7789_2_DRIVER)
    #error "board.h selects an ST7789 panel but TFT_eSPI's User_Setup.h does not define ST7789_DRIVER. Copy config/User_Setup_2432S028R_ST7789.h.template into the TFT_eSPI library."
  #endif
#elif CYD_PANEL_DRIVER == CYD_PANEL_ILI9341
  #if !defined(ILI9341_DRIVER) && !defined(ILI9341_2_DRIVER)
    #error "board.h selects an ILI9341 panel but TFT_eSPI's User_Setup.h does not define it. Copy config/User_Setup_2432S028R.h.template into the TFT_eSPI library."
  #endif
#endif
#if TFT_WIDTH != CYD_PANEL_W || TFT_HEIGHT != CYD_PANEL_H
  #error "User_Setup.h's TFT_WIDTH/TFT_HEIGHT disagree with board.h."
#endif
#if TFT_MOSI != CYD_TFT_MOSI_PIN || TFT_SCLK != CYD_TFT_SCLK_PIN || \
    TFT_CS != CYD_TFT_CS_PIN || TFT_DC != CYD_TFT_DC_PIN
  #error "User_Setup.h's TFT pins disagree with board.h."
#endif

// ---------------------------------------------------------------------------

static void centreMessage(const char* line1, const char* line2, uint16_t colour) {
  const Palette& p = theme();
  tft.fillScreen(p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(colour, p.bg);
  tft.drawString(line1, tft.width() / 2, tft.height() / 2 - 14, 4);
  if (line2) {
    tft.setTextColor(p.dim, p.bg);
    tft.drawString(line2, tft.width() / 2, tft.height() / 2 + 18, 2);
  }
}

// Where to pick up. Normally the saved track and position; but if the last run
// ended in a crash or a watchdog reset, that spot may be what crashed it (one
// MP3 wedged the decoder at its resume point and the board reset on every
// boot), so start the NEXT track from its beginning instead.
static void resumePoint(int& track, uint32_t& ms) {
  const int count = storage_trackCount();
  const int saved = g_settings.lastPath.length() ? storage_findTrack(g_settings.lastPath) :
                    (storage_catalogRebuilt() ? -1 : int(g_settings.lastTrack));
  const bool resumable = saved >= 0 && saved < count;
  track = resumable ? saved : 0;
  ms = resumable ? g_settings.lastPosMs : 0;
  const esp_reset_reason_t why = esp_reset_reason();
  if (why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT ||
      why == ESP_RST_WDT) {
    Serial.printf("boot: last run crashed (reason %d) -- skipping track %d\n", (int)why, track);
    track = count ? (track + 1) % count : 0;
    ms = 0;
  }
}

static void libraryProgress(const char* phase, int done, int total) {
  const Palette& p = theme();
  static char shown[24];
  if (strcmp(shown, phase)) {                    // new phase: new screen text
    strlcpy(shown, phase, sizeof(shown));
    centreMessage("CYDpod", phase, p.accent);
  }
  tft.fillRect(0, tft.height() / 2 + 34, tft.width(), 20, p.bg);
  tft.setTextColor(p.dim, p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(total > 0 ? String(done) + " / " + String(total) : total < 0 ? String(done) + "%" : String(done) + " songs",
                 tft.width() / 2, tft.height() / 2 + 44, 2);
}

static void titleProgress(int done, int total) {
  const Palette& p = theme();
  if (done == 1) centreMessage("CYD MP3", "Preparing song titles", p.accent);
  tft.fillRect(0, tft.height() / 2 + 34, tft.width(), 20, p.bg);
  tft.setTextColor(p.dim, p.bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(String(done) + " / " + String(total), tft.width() / 2, tft.height() / 2 + 44, 2);
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== CYD MP3 player %s ===\n", FIRMWARE_LABEL);
  cydPrintBanner();

  settings_begin();
#ifndef CYD_UPLOADER
  bluetoothPlanned = boot_plan(g_settings.outRoute, CYD_WIRED_OUTPUT).bluetooth;
  if (!bluetoothPlanned) {
    // The linked Classic stack reserves controller/host RAM even before start.
    // Releasing it is safe for this whole boot: switching output always restarts.
    if (!btMemRelease(BT_MODE_CLASSIC_BT)) Serial.println("bt: memory release failed");
    Serial.println("bt: skipped (headphones)");
  }
#endif
  Serial.printf("heap: after settings %u\n", ESP.getFreeHeap());
  cydRgbLedOff();
  display_begin();
  touch_begin();
  Serial.printf("heap: after display+touch %u\n", ESP.getFreeHeap());
  display_bootGesture();                 // blind panel-controller switch, 8 s hold

  centreMessage("CYD MP3", FIRMWARE_LABEL, theme().accent);
  display_setBacklight(g_settings.brightness);

  if (!touch_hasStoredCalibration()) calibrate_run();

#ifdef CYD_UPLOADER
  // The uploader image: WiFi file manager only. Does not return.
  while (!storage_mount() || storage_readOnly()) {
    centreMessage(storage_readOnly() ? "SD card is read-only" : "No SD card",
                  storage_readOnly() ? "the uploader needs a writable card" : "insert a FAT32 card", theme().warn);
    delay(2000);
  }
  uploadmode_run();
}

void loop() {}

#else  // the player image

  while (!storage_begin(libraryProgress)) {
    centreMessage("No SD card", "insert a FAT32 card with /music", theme().warn);
    delay(2000);
  }

  Serial.printf("heap: after storage %u\n", ESP.getFreeHeap());

  // A card that mounts but cannot be written (CT-07), or a rebuild that failed (CT-04):
  // say so for a moment, then carry on with whatever catalogue there is.
  if (storage_readOnly()) {
    centreMessage("SD card is read-only", "library may be out of date", theme().warn);
    delay(2500);
  } else if (storage_catalogStale()) {
    centreMessage("Library stale", "using the previous list", theme().warn);
    delay(2000);
  }

  // Folders and playlists first: their names are rendered with the titles.
  playlist_begin();
  Serial.printf("heap: after playlist %u\n", ESP.getFreeHeap());
  // Both need RAM only available before Bluetooth starts: see the header.
  titles_prepareAll(titleProgress);
  Serial.printf("heap before audio + bt: %u  largest %u\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());

  // Resume where the last session stopped (resumePoint above).
  int track;
  uint32_t ms;
  resumePoint(track, ms);
  audio_begin(track, ms, g_settings.volume, g_settings.shuffle, g_settings.repeat);
  playlist_restore(track);
  audio_start();
  const uint32_t audioWaitAt = millis();
  while (!audio_startupComplete() && millis() - audioWaitAt < 5000) delay(10);
  Serial.printf("heap: after audio startup %u largest %u ready %d\n",
                ESP.getFreeHeap(), ESP.getMaxAllocHeap(), audio_startupComplete());
  if (bluetoothPlanned) {
    if (audio_startupComplete()) { bt_begin(); bluetoothStarted = true; }
    else Serial.println("bt: deferred until initial track open completes");
  }
  if (!speaker_begin(g_settings.outRoute)) {
    // Keep the saved plan: a failed wired allocation must not secretly start
    // Bluetooth, nor overwrite the route and create a reboot loop.
    Serial.println("speaker: startup failed; saved output retained");
  }

  // Only the Bluetooth route has anything to set up before it can play; the
  // on-board speaker just works, so it lands on the player.
  Serial.printf("heap: after init %u min %u largest %u\n",
                ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
  const bool needSpeaker = bluetoothStarted && g_settings.outRoute == OUT_BT &&
                           (bt_pairing() || bt_speakerName().isEmpty());
  classic_home();
  ui_go(needSpeaker ? SCR_BLUETOOTH : SCR_CLASSIC);
}

// Keep the resume point current: at once when the track changes, every 30 s
// while playing, and when playback pauses.
static void persistResume(uint32_t now) {
  static uint32_t lastSave = 0;
  static bool     wasPlaying = true;
  if (audio_trackSerial() == 0) return;          // start track not open yet
  const int      track = audio_currentTrack();
  const uint32_t pos = audio_positionMs();
  const bool     playing = audio_isPlaying();
  bool save = false;
  if (track != g_settings.lastTrack) save = true;
  else if (playing && now - lastSave >= 30000) save = true;
  else if (!playing && wasPlaying) save = true;
  wasPlaying = playing;
  if (!save) return;
  g_settings.lastTrack = track;
  g_settings.lastPath = storage_trackPath(track);
  g_settings.lastPosMs = pos;
  settings_saveResume();
  lastSave = now;
}

// The route as the periodic stat line shows it. Boards without the wired route keep the
// original two-way expression.
#if CYD_WIRED_OUTPUT
#define STAT_ROUTE_LABEL (g_settings.outRoute == OUT_WIRED ? (wired_active() ? "wired" : "wired-off") : \
                          g_settings.outRoute == OUT_SPEAKER ? (speaker_active() ? "spk" : "spk-off") : \
                          (bt_connected() ? "bt" : "no-bt"))
#else
#define STAT_ROUTE_LABEL (g_settings.outRoute == OUT_SPEAKER ? (speaker_active() ? "spk" : "spk-off") : \
                          (bt_connected() ? "bt" : "no-bt"))
#endif

void loop() {
  const uint32_t now = millis();

  if (bluetoothPlanned && !bluetoothStarted && audio_startupComplete()) { bt_begin(); bluetoothStarted = true; }
  if (bluetoothStarted) bt_tick();
  audio_tick();
  persistResume(now);

  int x = 0, y = 0;
  const TouchEvent ev = touch_poll(&x, &y);
  // A drag is routed like any other gesture -- ui_touch() swallows it -- even
  // when it ran long enough to read as the recalibrate hold.
  if (ev == TOUCH_RECALIBRATE && !ui_pressClaimed() && !sleep_touchBlocked()) {
    calibrate_run();
    ui_redraw();
  } else if (ev != TOUCH_NONE) {
    ui_touch(ev, x, y);
  }

  ui_tick(now);

  static uint32_t lastStat = 0;
  if (now - lastStat >= 10000) {
    Serial.printf("diag: sd read max %u ms  dac stalls %u\n",
                  audio_takeMaxReadMs(), speaker_stalls());
    const AudioCommands commands=audio_commands();
    Serial.printf("stat: heap %u min %u largest %u  stacks dec %u loop %u  buf %u%% underruns %u pullMax %d pendingTrack %d pendingSeek %ld cmdSerial %lu starting %d seeking %d blocked %d  "
                  "%s  %s  pulls %u frames %u\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                  audio_decodeStackFree(), (unsigned)uxTaskGetStackHighWaterMark(nullptr),
                  audio_bufferPct(), audio_underruns(), audio_maxRequest(),commands.track,(long)commands.seekMs,(unsigned long)commands.serial,commands.opening,commands.seeking,commands.blocked,
                  STAT_ROUTE_LABEL,
                  audio_isPlaying() ? "playing" : "paused", audio_pullCalls(), audio_pullFrames());
#if CYD_WIRED_OUTPUT
    {
      const WiredStats w = wired_stats();
      Serial.printf("stat: wired %s frames %u chunks %u underruns %u errors %u stalls %u dropped %u sessions %u stack %u heapCost %u\n",
                    w.active ? "on" : "off", (unsigned)w.frames, (unsigned)w.chunks, (unsigned)w.underruns,
                    (unsigned)w.writeErrors, (unsigned)w.stalls, (unsigned)w.dropped, (unsigned)w.sessions,
                    (unsigned)w.stackFree, (unsigned)w.heapCost);
    }
#endif
    const AudioPcmStats pcm=audio_pcmStats();
    Serial.printf("stat: pcm input %lu output 44100 pumpMaxMs %lu yields %lu resampleBlockUs %lu resampleMaxUs %lu taps 32 phases 256\n",
                  (unsigned long)pcm.inputRate,(unsigned long)pcm.maxPumpMs,(unsigned long)pcm.yields,(unsigned long)pcm.blockUs,(unsigned long)pcm.maxBlockUs);
    // Touch: longest gap between polls since the last report (stat: loop above is
    // stack, not time), then cumulative reasons a recognised touch did nothing.
    cydtouch::Stats& ts = cydtouch::stats();
    Serial.printf("stat: touch pollGapMax %u ms  refused slop %u claim %u rearm %u wake %u  bridged %u\n",
                  (unsigned)ts.maxGapMs, (unsigned)ts.slop, (unsigned)ts.claim, (unsigned)ts.rearm, (unsigned)ts.wake, (unsigned)ts.bridged);
    classic_logScrollStats();
    ts.maxGapMs = 0;
    lastStat = now;
  }
  delay(2);
}

#endif  // CYD_UPLOADER
