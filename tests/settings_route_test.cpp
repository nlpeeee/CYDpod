// CY-71: the Output setting survives a reboot, a saved speaker choice migrates to Bluetooth, and a
// forced test route never overwrites what the user saved. Compiles the real settings.cpp against a
// map-backed Preferences. Three builds (build_wired_fade.sh): wired board, wired board with
// -DCYD_DEFAULT_OUT_ROUTE=2 (the test image), and a board without the module.
#include <Arduino.h>
#include <Preferences.h>
#include "../firmware/cydpod_player/app/settings.cpp"

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void reboot() {
  g_settings = Settings();
  settings_begin();
}

int main() {
  // First boot with empty NVS.
  Preferences::erase();
  reboot();
#if CYD_DEFAULT_OUT_ROUTE >= 0
  const uint8_t forced = outRouteSanitize(CYD_DEFAULT_OUT_ROUTE);
  CHECK(g_settings.outRoute == forced, "forced route %d expected, got %d", forced, g_settings.outRoute);
  CHECK(Preferences::peek("out") == OUT_BT, "first boot stored Bluetooth under the forced route, not %d", Preferences::peek("out"));
  settings_save();  // any settings change rewrites every key
  CHECK(Preferences::peek("out") == OUT_BT, "saving while forced must not write the forced route (%d)", Preferences::peek("out"));
  reboot();
  CHECK(g_settings.outRoute == forced, "forced again after reboot");
  // A choice saved by a normal build survives saves made while this image forces its own route.
  Preferences::poke("out", OUT_WIRED);
  reboot();
  g_settings.volume = 33;
  settings_save();
  CHECK(Preferences::peek("out") == OUT_WIRED, "saved Headphones kept (%d)", Preferences::peek("out"));
  Preferences::poke("out", OUT_BT);
  reboot();
  settings_save();
  CHECK(Preferences::peek("out") == OUT_BT, "saved Bluetooth kept (%d)", Preferences::peek("out"));
  // A menu choice other than the forced route is persisted. (Choosing the forced route itself
  // is not: this image re-forces it every boot, and that is what the test image is for.)
  g_settings.outRoute = OUT_BT == forced ? OUT_WIRED : OUT_BT;
  settings_save();
  CHECK(Preferences::peek("out") == g_settings.outRoute, "menu choice persisted (%d)", Preferences::peek("out"));
  printf("settings_route: forced-route build OK\n");
#else
  CHECK(g_settings.outRoute == OUT_BT, "fresh install selects Bluetooth, got %d", g_settings.outRoute);
  CHECK(Preferences::peek("out") == OUT_BT, "first boot writes Bluetooth");
#if CYD_WIRED_OUTPUT
  // The menu picks Headphones: it must be there after a reboot, then Bluetooth again.
  g_settings.outRoute = OUT_WIRED;
  settings_save();
  reboot();
  CHECK(g_settings.outRoute == OUT_WIRED, "Headphones persisted across reboot, got %d", g_settings.outRoute);
  g_settings.outRoute = OUT_BT;
  settings_save();
  reboot();
  CHECK(g_settings.outRoute == OUT_BT, "Bluetooth persisted across reboot, got %d", g_settings.outRoute);
  // Another save (volume) must not disturb it.
  g_settings.outRoute = OUT_WIRED;
  settings_save();
  reboot();
  g_settings.volume = 12;
  settings_save();
  reboot();
  CHECK(g_settings.outRoute == OUT_WIRED && g_settings.volume == 12, "unrelated saves keep the route");
  printf("settings_route: wired board OK\n");
#else
  printf("settings_route: board without the module OK\n");
#endif
  // Values from older or foreign builds: speaker (0), unknown, erased.
  const uint8_t odd[] = {OUT_SPEAKER, 3, 5, 200, 255};
  for (uint8_t v : odd) {
    Preferences::poke("out", v);
    reboot();
    CHECK(g_settings.outRoute == OUT_BT, "stored %d must read back as Bluetooth, got %d", v, g_settings.outRoute);
  }
  Preferences::poke("out", OUT_WIRED);
  reboot();
#if CYD_WIRED_OUTPUT
  CHECK(g_settings.outRoute == OUT_WIRED, "stored Headphones is kept");
#else
  CHECK(g_settings.outRoute == OUT_BT, "Headphones saved by another build reads as Bluetooth here");
#endif
#endif
  // CY-72: restart transaction persists exact identity/position before output.
  Preferences::failOpen=true;
  CHECK(!settings_saveOutputResume(OUT_BT), "failed namespace open rejects restart");
  Preferences::failOpen=false;
  for(const char* key : {"track","pos","lastPath","srcKind","srcKey","catKind","catGroup","catCrc","out"}) {
    Preferences::poke("out",OUT_BT);
    g_settings.lastTrack=7;g_settings.lastPosMs=84567;g_settings.lastPath="/music/resume.m4a";
    Preferences::failKey=key;
    CHECK(!settings_saveOutputResume(OUT_WIRED), "failed %s rejects restart",key);
    CHECK(Preferences::peek("out")==OUT_BT,"failed %s keeps old boot route",key);
    Preferences::failKey.clear();
  }
  g_settings.srcKind=1;g_settings.srcKey=12345;
  CHECK(settings_saveOutputResume(OUT_BT), "output resume committed");
  reboot();
  CHECK(g_settings.lastTrack==7 && g_settings.lastPosMs==84567 && !strcmp(g_settings.lastPath.c_str(),"/music/resume.m4a"),"restart restores same track and position");
  CHECK(g_settings.srcKind==1 && g_settings.srcKey==12345,"restart preserves queue source identity");
  // Empty-library output switching must distinguish failed path deletion from
  // putString("")'s ambiguous zero return, and must not retain an old identity.
  g_settings.lastPath="";Preferences::failKey="lastPath";
  CHECK(!settings_saveOutputResume(OUT_BT),"failed empty-path clear rejects restart");
  Preferences::failKey.clear();
  CHECK(settings_saveOutputResume(OUT_BT),"empty path clears successfully");
  CHECK(!prefs.isKey("lastPath"),"old track identity removed");
  CHECK(settings_saveOutputResume(OUT_BT),"already absent identity is valid");
  reboot();CHECK(g_settings.lastPath.isEmpty(),"empty-library identity remains empty at boot");
  if (failures) {
    printf("SETTINGS_ROUTE_FAILED %d\n", failures);
    return 1;
  }
  printf("SETTINGS_ROUTE_OK\n");
  return 0;
}
