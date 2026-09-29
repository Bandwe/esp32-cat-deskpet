#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdio>
inline uint32_t espTestMillis=0;
inline uint32_t millis() { return espTestMillis; }
