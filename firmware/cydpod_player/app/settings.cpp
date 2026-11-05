#include "settings.h"
#include "speaker.h"        // OutRoute
#include <Preferences.h>

Settings g_settings;

static Preferences prefs;
#if CYD_DEFAULT_OUT_ROUTE >= 0
static uint8_t storedOutRoute = 0;   // what NVS held at boot, before the forced route replaced it
#endif
static const char* NVS_NS = "mp3cfg";

void settings_begin() {
  // Opened read-write even though nothing is written: opening a namespace that
  // does not exist yet read-only logs an NVS error on every first boot.
  prefs.begin(NVS_NS, false);
  Settings& s = g_settings;
  s.panel      = prefs.getUShort("panel",  s.panel);
  s.invert     = prefs.getBool  ("invert", s.invert);
  s.rotation   = prefs.getUChar ("rot",    s.rotation) & 3;
  s.brightness = constrain(prefs.getUChar("bright", s.brightness), 10, 100);
  s.lang       = prefs.getUChar ("lang",   s.lang) % LANG_COUNT;
  s.screenTimeout = min<uint8_t>(prefs.getUChar("screenTime",s.screenTimeout),3);
  s.sleepTimer = min<uint8_t>(prefs.getUChar("sleepTime",s.sleepTimer),4);
  s.volume     = min<uint8_t>(prefs.getUChar("vol", s.volume), 100);
  s.outRoute   = outRouteSanitize(prefs.getUChar("out", s.outRoute));
#if CYD_DEFAULT_OUT_ROUTE >= 0
  storedOutRoute = s.outRoute;
#endif
  s.lastTrack  = prefs.getUShort("track",  s.lastTrack);
  s.lastPosMs  = prefs.getULong ("pos",    s.lastPosMs);
  s.lastPath   = prefs.getString("lastPath", s.lastPath);
  s.shuffle    = prefs.getBool  ("shuffle", s.shuffle);
  s.repeat     = prefs.getUChar ("repeat", s.repeat) % REPEAT_MODE_COUNT;
  s.srcKind    = prefs.getUChar ("srcKind", s.srcKind);
  s.srcKey     = prefs.getULong ("srcKey", s.srcKey);
  s.catalogKind = prefs.getUChar("catKind", s.catalogKind);
  s.catalogGroup = prefs.getUShort("catGroup", s.catalogGroup);
  s.catalogCrc = prefs.getULong("catCrc", s.catalogCrc);
  s.btName     = prefs.getString("btName", s.btName);
  prefs.getBytes("btAddr", s.btAddr, sizeof(s.btAddr));
  // One blob for the lot. A blob of another size is from another version of
  // this struct: ignore it rather than reading it as garbage.
  if (prefs.getBytesLength("known") == sizeof(s.btKnown)) {
    prefs.getBytes("known", s.btKnown, sizeof(s.btKnown));
    s.btKnownCount = min<uint8_t>(prefs.getUChar("knownN", 0), BT_KNOWN_MAX);
  }
  s.wifiSsid   = prefs.getString("ssid",   s.wifiSsid);
  s.wifiPass   = prefs.getString("pass",   s.wifiPass);
  s.webPin     = prefs.getString("webPin", s.webPin);
  const bool migrateClassic = prefs.getUChar("uiRev", 0) < 1;
  prefs.end();
  // One deliberate UI migration; calibration, bonds, output and volume survive. The retired
  // Style, Layout (skin), Background, Theme and time-mode keys are not read, not written and
  // not erased: whatever an older release left under them is simply ignored, and stays put
  // so that flashing an older image back still finds its own values.
  if (migrateClassic) {
#if CYD_TOUCH_CAPACITIVE
    s.rotation = CYD_ROTATION; // W328C first boot: 320x240 landscape
    s.outRoute = OUT_BT;       // this target is used as a Bluetooth player
#else
    s.rotation = 0;
#endif
    settings_save();
  }

#if CYD_TOUCH_CAPACITIVE
  s.panel = CYD_PANEL_ST7789; // fixed controller; stale NVS cannot select ILI9341
#endif
#if CYD_DEFAULT_OUT_ROUTE >= 0
  // Test build (board.h): boot on this route whatever NVS holds. storedOutRoute (read
  // above) keeps the saved value so settings_save() does not overwrite it.
  s.outRoute = outRouteSanitize(CYD_DEFAULT_OUT_ROUTE);
  Serial.printf("cmd: route %u forced by CYD_DEFAULT_OUT_ROUTE (saved route %u stays in NVS)\n",
                s.outRoute, storedOutRoute);
#endif
  // The WiFi password is never printed: serial logs end up in bug reports.
  Serial.printf("settings: lang %u  wifi \"%s\"\n", s.lang, s.wifiSsid.c_str());
  Serial.printf("settings: panel %u invert %d rot %u bright %u%%  vol %u resume %u@%lus "
                "shuffle %d repeat %u  out %u  speaker \"%s\"\n",
                s.panel, s.invert, s.rotation, s.brightness, s.volume, s.lastTrack,
                s.lastPosMs / 1000, s.shuffle, s.repeat, s.outRoute,
                s.btName.c_str());
  // The address as well as the name: the library keeps its OWN record of the
  // last device for its boot reconnect, and when the two disagree the player
  // reconnects to one device while the screen names another.
  Serial.printf("settings: speaker address %02x:%02x:%02x:%02x:%02x:%02x\n",
                s.btAddr[0], s.btAddr[1], s.btAddr[2], s.btAddr[3], s.btAddr[4], s.btAddr[5]);
  for (int i = 0; i < s.btKnownCount; i++)
    Serial.printf("settings: known %d \"%s\" %02x:%02x:%02x:%02x:%02x:%02x\n", i + 1,
                  s.btKnown[i].name, s.btKnown[i].addr[0], s.btKnown[i].addr[1],
                  s.btKnown[i].addr[2], s.btKnown[i].addr[3], s.btKnown[i].addr[4],
                  s.btKnown[i].addr[5]);
}

void settings_save() {
  const Settings& s = g_settings;
  prefs.begin(NVS_NS, false);
  prefs.putUChar("uiRev", 1);
  prefs.putUChar("catKind", s.catalogKind);
  prefs.putUShort("catGroup", s.catalogGroup);
  prefs.putULong("catCrc", s.catalogCrc);
  prefs.putUShort("panel",    s.panel);
  prefs.putBool  ("invert",   s.invert);
  prefs.putUChar ("rot",      s.rotation);
  prefs.putUChar ("bright",   s.brightness);
  prefs.putUChar ("lang",     s.lang);
  prefs.putUChar ("screenTime",s.screenTimeout);
  prefs.putUChar ("sleepTime",s.sleepTimer);
  prefs.putUChar ("vol",      s.volume);
#if CYD_DEFAULT_OUT_ROUTE >= 0
  // Writing the forced route would clobber the saved choice; only a route the user
  // picked in the UI (anything other than the forced one) is persisted.
  if (s.outRoute != outRouteSanitize(CYD_DEFAULT_OUT_ROUTE)) storedOutRoute = s.outRoute;
  prefs.putUChar ("out",      storedOutRoute);
#else
  prefs.putUChar ("out",      s.outRoute);
#endif
  prefs.putUShort("track",    s.lastTrack);
  prefs.putULong ("pos",      s.lastPosMs);
  prefs.putString("lastPath", s.lastPath);
  prefs.putBool  ("shuffle",  s.shuffle);
  prefs.putUChar ("repeat",   s.repeat);
  prefs.putUChar ("srcKind",  s.srcKind);
  prefs.putULong ("srcKey",   s.srcKey);
  prefs.putString("btName",   s.btName);
  prefs.putBytes ("btAddr",   s.btAddr, sizeof(s.btAddr));
  prefs.putBytes ("known",    s.btKnown, sizeof(s.btKnown));
  prefs.putUChar ("knownN",   s.btKnownCount);
  prefs.putString("ssid",     s.wifiSsid);
  prefs.putString("pass",     s.wifiPass);
  prefs.putString("webPin",   s.webPin);
  prefs.end();
}

void settings_saveResume() {
  prefs.begin(NVS_NS, false);
  prefs.putUShort("track", g_settings.lastTrack);
  prefs.putULong ("pos",   g_settings.lastPosMs);
  prefs.putString("lastPath", g_settings.lastPath);
  prefs.end();
}

void settings_savePanel() {
  prefs.begin(NVS_NS, false);
  prefs.putUShort("panel", g_settings.panel);
  prefs.end();
}

// Preferences put* commits synchronously (nvs_commit); end closes the handle.
// The route is last so a partial resume write cannot select a new boot plan.
bool settings_saveOutputResume(uint8_t next) {
  if (!prefs.begin(NVS_NS, false)) return false;
  const Settings& s = g_settings;
  bool ok = prefs.putUShort("track", s.lastTrack) == sizeof(s.lastTrack);
  ok = ok && prefs.putULong("pos", s.lastPosMs) == sizeof(s.lastPosMs);
  // putString returns zero for BOTH an empty string and a failed commit.
  // Removing an old identity instead gives an unambiguous committed result.
  if (s.lastPath.length()) ok = ok && prefs.putString("lastPath", s.lastPath) == s.lastPath.length();
  else if (prefs.isKey("lastPath")) ok = ok && prefs.remove("lastPath");
  ok = ok && prefs.putUChar("srcKind", s.srcKind) == sizeof(s.srcKind);
  ok = ok && prefs.putULong("srcKey", s.srcKey) == sizeof(s.srcKey);
  ok = ok && prefs.putUChar("catKind", s.catalogKind) == sizeof(s.catalogKind);
  ok = ok && prefs.putUShort("catGroup", s.catalogGroup) == sizeof(s.catalogGroup);
  ok = ok && prefs.putULong("catCrc", s.catalogCrc) == sizeof(s.catalogCrc);
  ok = ok && prefs.putUChar("out", next) == sizeof(next);
  prefs.end();
#if CYD_DEFAULT_OUT_ROUTE >= 0
  if (ok) storedOutRoute = next;
#endif
  return ok;
}
