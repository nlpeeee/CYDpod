#include "settings.h"
#include "speaker.h"        // OutRoute
#include <Preferences.h>

Settings g_settings;

static Preferences prefs;
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
  s.theme      = prefs.getUChar ("theme",  s.theme);
  s.skin       = prefs.getUChar ("skin",   s.skin);
  s.style      = prefs.getUChar ("style",  s.style);
  s.lang       = prefs.getUChar ("lang",   s.lang) % LANG_COUNT;
  s.screenTimeout = min<uint8_t>(prefs.getUChar("screenTime",s.screenTimeout),3);
  s.sleepTimer = min<uint8_t>(prefs.getUChar("sleepTime",s.sleepTimer),4);
  s.volume     = min<uint8_t>(prefs.getUChar("vol", s.volume), 100);
  s.outRoute   = prefs.getUChar ("out",    s.outRoute) % OUT_ROUTE_COUNT;
  s.lastTrack  = prefs.getUShort("track",  s.lastTrack);
  s.lastPosMs  = prefs.getULong ("pos",    s.lastPosMs);
  s.lastPath   = prefs.getString("lastPath", s.lastPath);
  s.timeMode   = prefs.getUChar ("timeMode", s.timeMode) % TIME_MODE_COUNT;
  s.shuffle    = prefs.getBool  ("shuffle", s.shuffle);
  s.repeat     = prefs.getUChar ("repeat", s.repeat) % REPEAT_MODE_COUNT;
  s.srcKind    = prefs.getUChar ("srcKind", s.srcKind);
  s.srcKey     = prefs.getULong ("srcKey", s.srcKey);
  s.catalogKind = prefs.getUChar("catKind", s.catalogKind);
  s.catalogGroup = prefs.getUShort("catGroup", s.catalogGroup);
  s.catalogCrc = prefs.getULong("catCrc", s.catalogCrc);
  s.bgIndex    = prefs.getUChar ("bg",     s.bgIndex);
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
  // One deliberate UI migration; calibration, bonds, output and volume survive.
  if (migrateClassic) {
#if CYD_TOUCH_CAPACITIVE
    s.rotation = CYD_ROTATION; // W328C first boot: 320x240 landscape
    s.outRoute = OUT_BT;       // this target is used as a Bluetooth player
#else
    s.rotation = 0;
#endif
    s.theme = 8; s.style = 7; s.bgIndex = 0;
    settings_save();
  }

#if CYD_TOUCH_CAPACITIVE
  s.panel = CYD_PANEL_ST7789; // fixed controller; stale NVS cannot select ILI9341
#endif
  // The WiFi password is never printed: serial logs end up in bug reports.
  Serial.printf("settings: theme %u skin %u style %u lang %u  wifi \"%s\"\n", s.theme, s.skin,
                s.style, s.lang, s.wifiSsid.c_str());
  Serial.printf("settings: panel %u invert %d rot %u bright %u%%  vol %u resume %u@%lus "
                "shuffle %d repeat %u time %u bg %u  out %u  speaker \"%s\"\n",
                s.panel, s.invert, s.rotation, s.brightness, s.volume, s.lastTrack,
                s.lastPosMs / 1000, s.shuffle, s.repeat, s.timeMode, s.bgIndex, s.outRoute,
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
  prefs.putUChar ("theme",    s.theme);
  prefs.putUChar ("skin",     s.skin);
  prefs.putUChar ("style",    s.style);
  prefs.putUChar ("lang",     s.lang);
  prefs.putUChar ("screenTime",s.screenTimeout);
  prefs.putUChar ("sleepTime",s.sleepTimer);
  prefs.putUChar ("vol",      s.volume);
  prefs.putUChar ("out",      s.outRoute);
  prefs.putUShort("track",    s.lastTrack);
  prefs.putULong ("pos",      s.lastPosMs);
  prefs.putString("lastPath", s.lastPath);
  prefs.putUChar ("timeMode", s.timeMode);
  prefs.putBool  ("shuffle",  s.shuffle);
  prefs.putUChar ("repeat",   s.repeat);
  prefs.putUChar ("srcKind",  s.srcKind);
  prefs.putULong ("srcKey",   s.srcKey);
  prefs.putUChar ("bg",       s.bgIndex);
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
