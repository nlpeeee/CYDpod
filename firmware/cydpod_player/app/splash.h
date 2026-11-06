// splash.h -- the startup screen: CYDpod wordmark on yellow, portrait or landscape.
#pragma once

// Draws the splash at the panel's current size (tft.width()/height(), so it follows the rotation).
// `label` (the firmware version) is printed small in ink near the bottom; pass nullptr for none.
// Call after display_begin(); the backlight is the caller's business.
void splash_show(const char* label);

// Player boot only: first frame, bounded updates, warning (no animation), finish.
void splash_start();
void splash_progress(const char* phase, int percent); // -1 = unknown
void splash_tick();                                // throttled to 125 ms
void splash_warning(const char* phase, const char* detail);
void splash_finish();                              // wait only the shortfall to 600 ms
