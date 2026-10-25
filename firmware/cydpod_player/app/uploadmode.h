// ===========================================================================
// uploadmode.h -- WiFi file manager: add songs, backgrounds and the title font
// ===========================================================================
// A separate FIRMWARE image, not a screen (firmware.h explains why): WiFi and
// Bluetooth never share a heap, or even a binary. "Upload music" in Setup
// restarts into this image; "Done" restarts back into the player, which
// re-indexes the library, renders new titles and converts new backgrounds on
// its way up.
//
// Networking, the same arrangement as the weather clock's setup portal:
//   - saved home WiFi joins as a station; the screen shows its address
//   - no saved WiFi, or joining fails: the player raises its own open hotspot
//     "CYD-MP3-XXXX" with a captive portal, so a phone that joins it opens the
//     page by itself. Uploading works over the hotspot too -- no home network
//     needed at all -- and the page's WiFi tab saves a home network.
//
// Every page and API sits behind the 6-digit web passcode, one session at a
// time (auth.h). The hotspot network itself stays open.
#pragma once

#include <Arduino.h>

void uploadmode_run();                 // the uploader image's whole life (does not return)
