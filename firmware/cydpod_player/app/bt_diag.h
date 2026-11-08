#pragma once
// AirPods investigation (docs/research/airpods-diag.md): A2DP link telemetry
// and serial `btd` commands. The Bluedroid counters exist only in a libbt
// built with tools/bluedroid/diag/0002-a2dp-src-diagnostics.patch; with any
// other libbt they read as missing and the commands that need them say so.
#include <esp_gap_bt_api.h>

bool btdiag_command(const char* line);   // "btd ..." serial commands
void btdiag_gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);  // Bluetooth task
void btdiag_tick(bool connected, const uint8_t* peer);  // loop task
