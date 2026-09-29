#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
using std::min;
using std::max;
#ifndef __GNUC__
#define __attribute__(...)
#endif
inline uint32_t micros() { return 0x13579bdu; }
