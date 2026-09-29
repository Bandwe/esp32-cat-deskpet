#pragma once
#include <stdint.h>

// Touch intent for the black-cat eyes. Pure C++ so contact geometry can be
// exercised on a host without the LCD, I2C controller, or Arduino runtime.
// The firmware's TouchGesture remains the authority for tap/hold/menu actions.
class CatTouchModel {
 public:
  enum class Mode : uint8_t { None, Look, Stroke, Drag };

  static bool faceZone(uint16_t x, uint16_t y, float faceX, float faceY) {
    const float dx = x-faceX, dy = y-faceY;
    return dx >= -86 && dx <= 86 && dy >= -58 && dy <= 39;
  }

  void down(uint16_t x, uint16_t y, float faceX, float faceY, uint32_t now) {
    _active = true;
    _mode = Mode::Look;
    _releasedMode = Mode::None;
    _startX = _x = x;
    _startY = _y = y;
    _downAt = now;
    _strokeTravel = 0;
    const float dx = x-faceX, dy = y-faceY;
    const bool nearPair = dx >= -86 && dx <= 86;
    _zone = nearPair && dy >= -58 && dy < -20 ? Zone::Brow :
            nearPair && dy >= -20 && dy <= 39 ? Zone::Eyes : Zone::Outside;
  }

  // Returns true only on the first frame of a recognized stroke or drag.
  bool move(uint16_t x, uint16_t y) {
    if (!_active) return false;
    const int dx = static_cast<int>(x)-_startX;
    const int dy = static_cast<int>(y)-_startY;
    const int ax = dx < 0 ? -dx : dx;
    const int ay = dy < 0 ? -dy : dy;
    bool started = false;
    const bool moved = ax > 12 || ay > 12 || dx*dx+dy*dy > 12*12;
    if (_mode == Mode::Look && moved && _zone != Zone::Outside) {
      _mode = _zone == Zone::Brow && 2*ax >= 3*ay ? Mode::Stroke : Mode::Drag;
      started = true;
    }
    if (_mode == Mode::Stroke) {
      const int step = static_cast<int>(x)-_x;
      _strokeTravel += step < 0 ? -step : step;
      if (_strokeTravel > 220) _strokeTravel = 220;
    }
    _x = x;
    _y = y;
    return started;
  }

  void release(uint32_t now) {
    if (!_active) return;
    _active = false;
    _releaseAt = now;
    _releasedMode = _mode;
    _mode = Mode::None;
  }

  void cancel() {
    _active = false;
    _mode = _releasedMode = Mode::None;
    _strokeTravel = 0;
  }

  bool active() const { return _active; }
  Mode mode() const { return _mode; }
  Mode releasedMode() const { return _releasedMode; }
  uint16_t x() const { return _x; }
  uint16_t y() const { return _y; }
  int dragX() const { return _mode == Mode::Drag ? static_cast<int>(_x)-_startX : 0; }
  int dragY() const { return _mode == Mode::Drag ? static_cast<int>(_y)-_startY : 0; }
  float strokeStrength() const {
    const float v = _strokeTravel / 95.0f;
    return v < 1 ? v : 1;
  }
  uint32_t downAt() const { return _downAt; }
  uint32_t releaseAt() const { return _releaseAt; }

 private:
  enum class Zone : uint8_t { Outside, Brow, Eyes };
  uint16_t _startX = 0, _startY = 0, _x = 0, _y = 0;
  uint32_t _downAt = 0, _releaseAt = 0;
  int _strokeTravel = 0;
  bool _active = false;
  Zone _zone = Zone::Outside;
  Mode _mode = Mode::None, _releasedMode = Mode::None;
};
