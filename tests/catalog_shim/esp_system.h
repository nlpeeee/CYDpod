#pragma once
#include <cstdint>
inline uint32_t esp_random(){static uint32_t v=42;v=v*1664525u+1013904223u;return v;}
