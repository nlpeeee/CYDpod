// ===========================================================================
// bt.h -- Bluetooth A2DP source: the speaker link
// ===========================================================================
// Startup pages the last saved address once; later reconnects are bounded and
// owner-gated (bt_reconnect_policy.h).
// Pair new and Cancel run live on the existing Bluetooth event task. Neither
// operation reboots or reinitializes the controller; bonds remain until Forget.
#pragma once

#include <Arduino.h>
#include "pairing_session.h"

static const int BT_MAX_DEVICES = 8;

struct BtDevice {
  char    name[33];
  uint8_t addr[6];
  int     rssi;
};

void   bt_begin();
void   bt_tick();                     // call from loop()

bool   bt_connected();
bool   bt_connecting();
String bt_speakerName();

// How a connection the owner asked for ended, if it did not succeed.
enum BtFailure : uint8_t {
  BT_FAIL_NONE = 0,
  BT_FAIL_TIMEOUT,                    // nothing came back in time
  BT_FAIL_AUTH,                       // the device refused the pairing
  BT_FAIL_RADIO,                      // a Bluetooth call failed (not a timeout)
};

// One bounded view for the UI. Saved rows precede discovered rows.
struct BtUiStatus {
  char name[33];
  uint8_t peer[6];
  BtFailure failure;
  cyd::PairingSession::Phase phase;
  uint32_t passkey;
  bool connected, connecting, choosing, pairing, disconnecting;
};
struct BtUiRow {
  BtDevice device;
  bool saved, connected;
};
struct BtUiSnapshot {
  BtUiStatus status;
  BtUiRow rows[BT_MAX_DEVICES];
  uint8_t count;
};
void bt_uiSnapshot(BtUiSnapshot& out);
bool bt_connectDevice(const uint8_t addr[6]);
bool bt_disconnect(const uint8_t addr[6]);
bool bt_forgetDevice(const uint8_t addr[6]);

// Pairing
cyd::PairingSession::Phase bt_pairingPhase();
bool   bt_pairing();              // live pairing session is active
bool   bt_requestPairing();           // queue live pairing; false if unavailable
bool   bt_cancelPairing();            // cancel live pairing; no reboot
int    bt_deviceCount();
bool   bt_device(int i, BtDevice& out);

// ---------------------------------------------------------------------------
// Devices connected before, most recent first (Settings::btKnown)
// ---------------------------------------------------------------------------
// A car and a speaker take turns, and neither can be found by discovery once
// it is out of pairing mode -- a car only advertises while its pairing screen
// is open. Keeping the addresses means either can be paged on request.
// Is this the device currently on the other end? By address: names are not
// unique, and the stored name lags a moment behind a switch.
bool   bt_isConnectedTo(const uint8_t addr[6]);

int    bt_knownCount();
bool   bt_known(int i, BtDevice& out);
void   bt_connectKnown(int i);        // page it now
void   bt_forget(int i);              // drop it from the list
uint32_t bt_listSerial();             // changes whenever the list does
void   bt_choose(int i);
bool   bt_choosing();                 // a device was tapped, waiting for it

// Why the last attempt the owner made failed; cleared by the next attempt.
BtFailure bt_failure();
// A Secure Simple Pairing code to compare with the one on the other device,
// or 0. A car shows one and waits; the owner cannot confirm what they cannot
// see, so the screen has to display it.
uint32_t  bt_passkey();
