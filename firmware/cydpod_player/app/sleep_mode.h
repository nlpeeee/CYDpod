#pragma once
#include <Arduino.h>
void sleep_begin(uint32_t now);
void sleep_tick(uint32_t now,bool down);
bool sleep_touchBlocked();
bool sleep_screenOff();
uint32_t sleep_idleMs(uint32_t now);     // since the last touch
void sleep_forceScreenOff();             // dark now, as the sleep timer does
bool sleep_takeTimerExpired();           // the sleep timer just paused playback (read once)
// Back from light sleep (power.cpp): light the screen; pressed = a touch woke
// it, so that press is swallowed like any other wake press.
void sleep_wakeNow(uint32_t now,bool pressed);
void sleep_setTimer(uint8_t choice,uint32_t now);
uint16_t sleep_remainingMinutes(uint32_t now);
const char* sleep_timeoutLabel(uint8_t choice);
const char* sleep_timerLabel(uint8_t choice);
