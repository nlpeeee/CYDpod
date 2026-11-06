// splash.h -- the startup screen: CYDpod wordmark on yellow, portrait or landscape.
#pragma once

// Draws the splash at the panel's current size (tft.width()/height(), so it follows the rotation).
// `label` (the firmware version) is printed small in ink near the bottom; pass nullptr for none.
// Call after display_begin(); the backlight is the caller's business.
void splash_show(const char* label);
