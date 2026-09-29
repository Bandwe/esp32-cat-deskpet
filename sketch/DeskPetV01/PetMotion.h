#pragma once

#include <math.h>
#include <stdint.h>

// Allocation-free whole-face motion, independent of Arduino and the renderer.
// Positions are pixel offsets from the renderer's neutral face pivot. Positive
// screen-space input acceleration pushes right/down; map IMU axes (and the
// desired opposite-to-device inertial sign) before calling observe(). Units:
// acceleration in g, angular velocity in degrees/second, time in milliseconds.
class PetMotion {
 public:
  void begin(uint32_t now, uint32_t seed = 1) {
    _rng = seed ? seed : 1;
    _x = _y = _vx = _vy = _ax = _ay = _gz = _tilt = 0;
    _impactX = _impactY = 0;
    _lastUpdateAt = _lastSampleAt = now;
    _resumeWanderAt = now;
    _lastCollisionAt = now - 101;
    _collisionCount = 0;
    _sleeping = false;
    _dizzy = false;
    _dizzyAt = now;
    _initialized = true;
    chooseTarget(now);
  }

  // Bounds must already allow for the complete face, expression deformation,
  // global scale, tilt and impact squash. This class only bounds its pivot.
  void setBounds(float maxOffsetX, float maxOffsetY) {
    _boundX = finiteValue(maxOffsetX) ? limit(maxOffsetX, 0, 120) : 0;
    _boundY = finiteValue(maxOffsetY) ? limit(maxOffsetY, 0, 140) : 0;
    _x = limit(_x, -_boundX, _boundX);
    _y = limit(_y, -_boundY, _boundY);
    _targetX = limit(_targetX, -_boundX, _boundX);
    _targetY = limit(_targetY, -_boundY, _boundY);
    if (_boundX == 0) _vx = 0;
    if (_boundY == 0) _vy = 0;
  }

  void observe(float axMotion, float ayMotion, float gzDps, uint32_t now) {
    if (!_initialized) begin(now);
    if (_sleeping) return;
    _ax = finiteValue(axMotion) ? limit(axMotion, -3.0f, 3.0f) : 0;
    _ay = finiteValue(ayMotion) ? limit(ayMotion, -3.0f, 3.0f) : 0;
    _gz = finiteValue(gzDps) ? limit(gzDps, -1000.0f, 1000.0f) : 0;
    // Remove tiny high-pass residuals, but do not require a full shake gesture:
    // face inertia responds continuously, independently of the dizzy trigger.
    if (fabsf(_ax) < .025f) _ax = 0;
    if (fabsf(_ay) < .025f) _ay = 0;
    _lastSampleAt = now;
    if (_ax * _ax + _ay * _ay > .0081f || fabsf(_gz) > 18) {
      _resumeWanderAt = now + 1200;
      _nextTargetAt = _resumeWanderAt;
    }
  }

  void update(uint32_t now, bool sleeping, bool dizzy = false) {
    if (!_initialized) begin(now);
    const uint32_t elapsed = now - _lastUpdateAt;
    _lastUpdateAt = now;
    // Never integrate a long pause as one large, explosive physics step.
    const float dt = (elapsed > 50 ? 50 : elapsed) * .001f;
    const float impactDecay = expf(-dt / .065f);
    _impactX *= impactDecay;
    _impactY *= impactDecay;

    if (sleeping) {
      _sleeping = true;
      _dizzy = false;
      _vx = _vy = _ax = _ay = _gz = 0;
      _targetX = _x;
      _targetY = _y;
      _tilt *= expf(-dt * 9);
      return;  // Preserve sleeping position; shaking never wakes the pet.
    }
    if (_sleeping) {
      _sleeping = false;
      _resumeWanderAt = now + 350;
      _nextTargetAt = _resumeWanderAt;
      _lastSampleAt = now;
    }
    if (dt <= 0) return;

    // A disconnected/stalled sensor must not keep applying its last force.
    const uint32_t sampleAge = now - _lastSampleAt;
    const float fresh = sampleAge < 80 ? 1.0f :
        (sampleAge < 160 ? (160 - sampleAge) / 80.0f : 0.0f);
    const float ax = _ax * fresh;
    const float ay = _ay * fresh;
    const float gz = _gz * fresh;
    const bool wander = !dizzy && static_cast<int32_t>(now - _resumeWanderAt) >= 0;
    if (wander && static_cast<int32_t>(now - _nextTargetAt) >= 0)
      chooseTarget(now);

    if (dizzy && !_dizzy) {
      _dizzyAt = now;
      // Start the six-second dizzy action with an immediate visible throw.
      // Prefer the current momentum, then the last measured force direction.
      float throwX = _vx, throwY = _vy;
      if (throwX * throwX + throwY * throwY < 80 * 80) {
        throwX = ax; throwY = ay;
      }
      if (throwX * throwX + throwY * throwY < .01f) {
        throwX = (randomWord() & 1) ? 1.0f : -1.0f;
        throwY = (randomWord() & 1) ? .75f : -.75f;
      }
      const float factor = 760.0f / sqrtf(throwX * throwX + throwY * throwY);
      _vx = throwX * factor;
      _vy = throwY * factor;
    }
    if (!dizzy && _dizzy) {
      _resumeWanderAt = now + 650;
      _nextTargetAt = _resumeWanderAt;
    }
    _dizzy = dizzy;
    // Fast throw -> several rebounds -> gentle drift over the dizzy action.
    // The decreasing envelope is time-based, not dependent on display FPS.
    const float dizzyProgress = limit((now - _dizzyAt) / 6000.0f, 0, 1);
    const float remaining = 1 - dizzyProgress;
    const float energy = remaining * remaining;
    float fx = ax * (dizzy ? 800.0f + 1100.0f * energy : 800.0f);
    float fy = ay * (dizzy ? 800.0f + 1100.0f * energy : 800.0f);
    if (wander) {
      // A softly damped spring is the idle position tween. It keeps moving
      // toward a screen-wide target, not back toward the center of the screen.
      fx += (_targetX - _x) * 4.0f;
      fy += (_targetY - _y) * 4.0f;
    }
    const float damping = expf(-dt * (dizzy ? .35f + 1.65f * dizzyProgress
                                          : (wander ? 3.8f : 1.35f)));
    _vx = (_vx + fx * dt) * damping;
    _vy = (_vy + fy * dt) * damping;
    const float speedSquared = _vx * _vx + _vy * _vy;
    const float maximumSpeed = dizzy ? 80.0f + 870.0f * energy : 500.0f;
    const float minimumSpeed = 40.0f + 580.0f * energy;
    if (speedSquared > maximumSpeed * maximumSpeed) {
      const float factor = maximumSpeed / sqrtf(speedSquared);
      _vx *= factor;
      _vy *= factor;
    } else if (dizzy && speedSquared < minimumSpeed * minimumSpeed) {
      if (speedSquared > 1) {
        const float factor = minimumSpeed / sqrtf(speedSquared);
        _vx *= factor;
        _vy *= factor;
      } else {
        _vx = ((randomWord() & 1) ? .8f : -.8f) * minimumSpeed;
        _vy = ((randomWord() & 1) ? .6f : -.6f) * minimumSpeed;
      }
    }
    // Short collision substeps prevent fast throws tunnelling across a wall.
    const uint8_t steps = static_cast<uint8_t>(ceilf(dt / .008f));
    const float step = dt / steps;
    for (uint8_t i = 0; i < steps; ++i) {
      _x += _vx * step;
      _y += _vy * step;
      collide(_x, _vx, _boundX, _impactX, now);
      collide(_y, _vy, _boundY, _impactY, now);
    }

    // Velocity lean and wrist rotation have a brief eased response rather than
    // snapping. Angular input affects tilt only; ax/ay drive wall collisions.
    const float desiredTilt = limit(_vx * .00035f + gz * .00012f, -.14f, .14f);
    _tilt += (desiredTilt - _tilt) * (1 - expf(-dt * 13));
  }

  float x() const { return _x; }
  float y() const { return _y; }
  float vx() const { return _vx; }
  float vy() const { return _vy; }
  float speed() const { return sqrtf(_vx * _vx + _vy * _vy); }
  float impactScaleX() const { return 1 - .12f * _impactX + .075f * _impactY; }
  float impactScaleY() const { return 1 - .12f * _impactY + .075f * _impactX; }
  float tilt() const { return _tilt; }
  uint32_t collisionCount() const { return _collisionCount; }

 private:
  static bool finiteValue(float value) {
    return value == value && value <= 1000000.0f && value >= -1000000.0f;
  }
  static float limit(float value, float lo, float hi) {
    return value < lo ? lo : (value > hi ? hi : value);
  }
  uint32_t randomWord() {
    _rng ^= _rng << 13;
    _rng ^= _rng >> 17;
    _rng ^= _rng << 5;
    return _rng;
  }
  float randomUnit() {
    return (randomWord() & 0x00ffffffU) / 16777216.0f;
  }
  void chooseTarget(uint32_t now) {
    // Full-range target distribution with a minimum useful travel distance.
    // Two bounded retries avoid excessive hovering near the current position.
    for (uint8_t attempt = 0; attempt < 3; ++attempt) {
      _targetX = (randomUnit() * 2 - 1) * _boundX * .96f;
      _targetY = (randomUnit() * 2 - 1) * _boundY * .96f;
      const float dx = _targetX - _x;
      const float dy = _targetY - _y;
      if (dx * dx + dy * dy >= 28 * 28) break;
    }
    _nextTargetAt = now + 2000 + randomWord() % 3001;
  }
  void collide(float& position, float& velocity, float bound,
               float& impact, uint32_t now) {
    if (bound <= 0) {
      position = velocity = 0;
      return;
    }
    const bool hitLow = position < -bound;
    const bool hitHigh = position > bound;
    if (!hitLow && !hitHigh) return;
    position = hitLow ? -bound : bound;
    // Do not reflect again if a boundary changed while already moving inward.
    if ((hitLow && velocity >= 0) || (hitHigh && velocity <= 0)) return;
    const float hitSpeed = fabsf(velocity);
    velocity *= _dizzy ? -.90f : -.65f;
    if (hitSpeed >= 35) {
      const float strength = limit(hitSpeed / 200, .25f, 1.0f);
      if (strength > impact) impact = strength;
      if (hitSpeed >= 45 && now - _lastCollisionAt >= 100) {
        ++_collisionCount;
        _lastCollisionAt = now;
      }
    }
  }

  float _x = 0, _y = 0, _vx = 0, _vy = 0;
  float _ax = 0, _ay = 0, _gz = 0, _tilt = 0;
  float _impactX = 0, _impactY = 0;
  float _boundX = 48, _boundY = 75;
  float _targetX = 0, _targetY = 0;
  uint32_t _rng = 1, _lastUpdateAt = 0, _lastSampleAt = 0;
  uint32_t _resumeWanderAt = 0, _nextTargetAt = 0;
  uint32_t _lastCollisionAt = 0, _collisionCount = 0;
  uint32_t _dizzyAt = 0;
  bool _sleeping = false, _initialized = false, _dizzy = false;
};
