#pragma once
#include <math.h>
#include <stdint.h>

// Unsigned subtraction handles millis() wraparound. Convert the elapsed time
// to float before negating it; negating uint32_t first turns a small duration
// into a huge positive exponent and can blank both eyes.
namespace CatTouchDecay {
inline float factor(uint32_t now, uint32_t since, float timeConstantMs) {
  const uint32_t elapsedMs = now - since;
  return expf(-static_cast<float>(elapsedMs) / timeConstantMs);
}
}
