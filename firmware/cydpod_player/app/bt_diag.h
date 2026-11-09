#pragma once
// A2DP link manager (docs/specs/bt-rate-and-earbuds.md): link telemetry, the
// adaptive SBC rate (bt_rate_policy.h), per-device link memory and the
// quiet-UI gate, plus the serial `btd` commands. The Bluedroid counters and
// rate control come from tools/bluedroid/patches/0002; with a libbt without
// it they read as missing and the rate stays stock.
#include <esp_gap_bt_api.h>

bool btdiag_command(const char* line);   // "btd ..." serial commands
void btdiag_gap(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);  // Bluetooth task
void btdiag_tick(bool connected, const uint8_t* peer);  // loop task
bool btlink_quietUi();            // keep marquee and coast animations still (earbuds, congestion, btd ui)
uint32_t btlink_idleSuspendMs();  // idle wait before suspending the connected device's stream
void btlink_droppedWhenIdle(const uint8_t addr[6]);  // loop task: that device ended the link after our suspend
