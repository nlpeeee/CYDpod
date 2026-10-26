#pragma once
#include <Arduino.h>
void sleep_begin(uint32_t now);
void sleep_tick(uint32_t now,bool down);
bool sleep_touchBlocked();
bool sleep_screenOff();
void sleep_setTimer(uint8_t choice,uint32_t now);
uint16_t sleep_remainingMinutes(uint32_t now);
const char* sleep_timeoutLabel(uint8_t choice);
const char* sleep_timerLabel(uint8_t choice);
