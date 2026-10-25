// ===========================================================================
// auth.h -- the 6-digit passcode in front of every WiFi page
// ===========================================================================
// WiFi image only: every page and API of upload mode sits behind it.
//
//   - The passcode is Settings::webPin, "000000" until the owner changes it
//     on the WiFi tab. Setup on the player shows it and can reset it, so
//     whoever holds the device can always get back in.
//   - One session at a time: a browser that signs in gets a random cookie,
//     and while it is in use nobody else can sign in. It ends on Sign out, or
//     after IDLE_MS without a request (the pages ping while they are open).
//   - Every wrong passcode doubles the wait before the next try is heard,
//     5 s up to 15 min. The count clears on a right one; it is kept in RAM,
//     and a restart needs the device in hand.
//
// The hotspot itself stays open: WPA2 needs at least 8 characters, so the
// passcode guards the pages, not the network.
#pragma once

#include <Arduino.h>
#include <WebServer.h>

static const uint32_t AUTH_IDLE_MS = 2 * 60 * 1000;

// Before server.begin(): asks the server to keep the Cookie header.
void auth_begin(WebServer& server);

// Wraps a handler so it only runs for the signed-in browser. Unsigned GETs of
// a page get the sign-in page; anything else gets 401 JSON.
WebServer::THandlerFunction auth_guard(WebServer::THandlerFunction fn, bool page = false);

// For upload data callbacks, which cannot answer: true when the request
// carries the live session.
bool auth_ok();

// /api/login, /api/logout, /api/passcode -- registered by auth_begin().

bool auth_validPin(const String& pin);      // exactly 6 digits
