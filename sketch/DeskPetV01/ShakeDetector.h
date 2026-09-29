#pragma once
#include <stdint.h>
#include <math.h>

// Handheld back-and-forth shaking: four confirmed excursions along one axis,
// not rotation alone or isolated sample spikes. No hardware dependency.
class ShakeDetector {
 public:
  explicit ShakeDetector(float threshold) : threshold_(threshold) {}
  void reset() { primed_ = false; candidate_ = false; reversals_ = 0; }
  uint8_t reversals() const { return reversals_; }

  bool update(float x, float y, float z, uint32_t now) {
    if (primed_ && (now - lastTurn_ > 450 || now - started_ > 1400)) reset();
    const float magnitude = sqrtf(x * x + y * y + z * z);
    if (!isfinite(magnitude) || magnitude < threshold_) {
      candidate_ = false;
      return false;
    }
    x /= magnitude; y /= magnitude; z /= magnitude;
    if (!primed_) {
      if (!confirmDirection(x, y, z, now)) return false;
      primed_ = true;
      started_ = lastTurn_ = now;
      refX_ = x; refY_ = y; refZ_ = z;
      return false;
    }
    // Keep the initial axis fixed; a tour through unrelated axes is not a shake.
    // Alternate the sign and require a clear projection into the opposite cone.
    const float alignment = x * refX_ + y * refY_ + z * refZ_;
    const float expectedProjection = (reversals_ & 1) ? alignment : -alignment;
    if (expectedProjection < .65f || now - lastTurn_ < 100) {
      candidate_ = false;
      return false;
    }
    if (!confirmDirection(x, y, z, now)) return false;
    lastTurn_ = now;
    if (++reversals_ < 3) return false;
    reset();
    return true;
  }

 private:
  bool confirmDirection(float x, float y, float z, uint32_t now) {
    const float alignment = x * candidateX_ + y * candidateY_ + z * candidateZ_;
    if (!candidate_ || now - candidateLast_ > 80 || alignment < .85f) {
      candidate_ = true;
      candidateAt_ = candidateLast_ = now;
      candidateX_ = x; candidateY_ = y; candidateZ_ = z;
      return false;
    }
    candidateLast_ = now;
    // At least two consecutive valid samples spanning 20 ms; do not count
    // a single-frame spike or combine samples separated by a sensor stall.
    if (now - candidateAt_ < 20) return false;
    candidate_ = false;
    return true;
  }

  float threshold_, refX_ = 0, refY_ = 0, refZ_ = 0;
  float candidateX_ = 0, candidateY_ = 0, candidateZ_ = 0;
  uint32_t started_ = 0, lastTurn_ = 0;
  uint32_t candidateAt_ = 0, candidateLast_ = 0;
  uint8_t reversals_ = 0;
  bool primed_ = false, candidate_ = false;
};
