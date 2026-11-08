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
  s.colorTheme = themeSanitize(prefs.getUChar("colorTheme", THEME_LIGHT));
  s.screenTimeout = min<uint8_t>(prefs.getUChar("screenTime",s.screenTimeout),3);
  s.sleepTimer = min<uint8_t>(prefs.getUChar("sleepTime",s.sleepTimer),4);
  s.volume     = min<uint8_t>(prefs.getUChar("vol", s.volume), 100);
  // Read the raw boolean byte: absent, wrong type/size or values >1 default On.
  s.headphoneLimit = prefs.getUChar("hpLimit", 1) != 0;
  s.eqOn = prefs.getUChar("eqOn", 0) == 1;
  // A blob of another size is not ours: keep the flat default.
  if (prefs.getBytesLength("eqGain") == sizeof(s.eqGain)) prefs.getBytes("eqGain", s.eqGain, sizeof(s.eqGain));
  for (int8_t& g : s.eqGain) g = g < -12 ? -12 : g > 12 ? 12 : g;
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

// Empty putString returns zero on both success and failure. Removal is checked.
static bool saveString(const char* key, const String& value) {
  if (value.length()) return prefs.putString(key, value) == value.length();
  return !prefs.isKey(key) || prefs.remove(key);
}

bool settings_save() {
  const Settings& s = g_settings;
  if (!prefs.begin(NVS_NS, false)) return false;
  bool ok = true;
  ok = (prefs.putUChar("uiRev", 1) == 1) && ok;
  ok = (prefs.putUChar("catKind", s.catalogKind) == 1) && ok;
  ok = (prefs.putUShort("catGroup", s.catalogGroup) == 2) && ok;
  ok = (prefs.putULong("catCrc", s.catalogCrc) == 4) && ok;
  ok = (prefs.putUShort("panel",    s.panel) == 2) && ok;
  ok = (prefs.putBool("invert",   s.invert) == 1) && ok;
  ok = (prefs.putUChar("rot",      s.rotation) == 1) && ok;
  ok = (prefs.putUChar("bright",   s.brightness) == 1) && ok;
  ok = (prefs.putUChar("lang",     s.lang) == 1) && ok;
  ok = (prefs.putUChar("colorTheme", themeSanitize(s.colorTheme)) == 1) && ok;
  ok = (prefs.putUChar("screenTime",s.screenTimeout) == 1) && ok;
  ok = (prefs.putUChar("sleepTime",s.sleepTimer) == 1) && ok;
  ok = (prefs.putBool("hpLimit", s.headphoneLimit) == 1) && ok;
  ok = (prefs.putUChar("vol",      s.volume) == 1) && ok;
  ok = (prefs.putUChar("eqOn",     s.eqOn ? 1 : 0) == 1) && ok;
  ok = (prefs.putBytes("eqGain",   s.eqGain, sizeof(s.eqGain)) == sizeof(s.eqGain)) && ok;
#if CYD_DEFAULT_OUT_ROUTE >= 0
  // Writing the forced route would clobber the saved choice; only a route the user
  // picked in the UI (anything other than the forced one) is persisted.
  if (s.outRoute != outRouteSanitize(CYD_DEFAULT_OUT_ROUTE)) storedOutRoute = s.outRoute;
  ok = (prefs.putUChar("out",      storedOutRoute) == 1) && ok;
#else
  ok = (prefs.putUChar("out",      s.outRoute) == 1) && ok;
#endif
  ok = (prefs.putUShort("track",    s.lastTrack) == 2) && ok;
  ok = (prefs.putULong("pos",      s.lastPosMs) == 4) && ok;
  ok = saveString("lastPath", s.lastPath) && ok;
  ok = (prefs.putBool("shuffle",  s.shuffle) == 1) && ok;
  ok = (prefs.putUChar("repeat",   s.repeat) == 1) && ok;
  ok = (prefs.putUChar("srcKind",  s.srcKind) == 1) && ok;
  ok = (prefs.putULong("srcKey",   s.srcKey) == 4) && ok;
  ok = saveString("btName", s.btName) && ok;
  ok = (prefs.putBytes("btAddr",   s.btAddr, sizeof(s.btAddr)) == sizeof(s.btAddr)) && ok;
  ok = (prefs.putBytes("known",    s.btKnown, sizeof(s.btKnown)) == sizeof(s.btKnown)) && ok;
  ok = (prefs.putUChar("knownN",   s.btKnownCount) == 1) && ok;
  ok = saveString("ssid", s.wifiSsid) && ok;
  ok = saveString("pass", s.wifiPass) && ok;
  ok = saveString("webPin", s.webPin) && ok;
  prefs.end();
  return ok;
}

bool settings_setHeadphoneLimit(bool on) {
  if (!prefs.begin(NVS_NS, false)) {
    Serial.println("headphone-limit: failed nvs=open");
    return false;
  }
  const bool ok = prefs.putBool("hpLimit", on) == 1;
  prefs.end();
  if (!ok) Serial.println("headphone-limit: failed nvs=hpLimit");
  else g_settings.headphoneLimit = on;
  return ok;
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
  if (next != outRouteSanitize(next)) return false;
  if (!prefs.begin(NVS_NS, false)) { Serial.println("output-switch: failed nvs=open"); return false; }
  const Settings& s = g_settings;
  const char* failed = nullptr;
  auto checked = [&](const char* key, bool ok) { if (!ok) failed = key; return ok; };
  bool ok = checked("track", prefs.putUShort("track", s.lastTrack) == sizeof(s.lastTrack));
  ok = ok && checked("pos", prefs.putULong("pos", s.lastPosMs) == sizeof(s.lastPosMs));
  // putString returns zero for BOTH an empty string and a failed commit.
  // Removing an old identity instead gives an unambiguous committed result.
  if (s.lastPath.length()) ok = ok && checked("lastPath", prefs.putString("lastPath", s.lastPath) == s.lastPath.length());
  else if (prefs.isKey("lastPath")) ok = ok && checked("lastPath", prefs.remove("lastPath"));
  ok = ok && checked("srcKind", prefs.putUChar("srcKind", s.srcKind) == sizeof(s.srcKind));
  ok = ok && checked("srcKey", prefs.putULong("srcKey", s.srcKey) == sizeof(s.srcKey));
  ok = ok && checked("catKind", prefs.putUChar("catKind", s.catalogKind) == sizeof(s.catalogKind));
  ok = ok && checked("catGroup", prefs.putUShort("catGroup", s.catalogGroup) == sizeof(s.catalogGroup));
  ok = ok && checked("catCrc", prefs.putULong("catCrc", s.catalogCrc) == sizeof(s.catalogCrc));
  ok = ok && checked("out", prefs.putUChar("out", next) == sizeof(next));
  prefs.end();
  if (failed) Serial.printf("output-switch: failed nvs=%s\n", failed);
#if CYD_DEFAULT_OUT_ROUTE >= 0
  if (ok) storedOutRoute = next;
#endif
  return ok;
}
