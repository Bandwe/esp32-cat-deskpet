#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "PetMotion.h"

enum class PetMood : uint8_t {
  Neutral, Happy, Sad, Surprised, Sleepy, Annoyed, Dizzy
};

// Offline, allocation-free face renderer. The caller owns canvas begin/flush,
// input events, mood hold durations, and the persistent sleeping state.
class PetFace {
public:
  void begin(Arduino_Canvas* canvas);
  void setMood(PetMood mood, uint32_t now);
  bool playAnimation(const char* name, uint32_t now);
  PetMood mood() const { return _mood; }
  const char* animationName() const;
  uint32_t animationsStarted() const { return _animationsStarted; }
  void observeMotion(float x, float y, float rotation, uint32_t now) {
    _motion.observe(x, y, rotation, now);
  }
  float positionX() const { return _positionX; }
  float positionY() const { return _positionY; }
  float motionSpeed() const { return _motion.speed(); }
  float eyeRoundness() const { return _pose.v[EyeRoundness]; }
  float leftLidDrop() const { return _pose.v[LeftLidDrop]; }
  float rightLidDrop() const { return _pose.v[RightLidDrop]; }
  uint32_t collisionCount() const { return _motion.collisionCount(); }
  int16_t drawTop() const { return _drawTop; }
  int16_t drawBottom() const { return _drawBottom; }
  void render(uint32_t now, bool sleeping = false);

private:
  enum Key : uint8_t {
    Height, Width, Curve, Tilt, Mouth, MouthWidth, GazeY, Round,
    Blush, Tear, GazeX, BodyX, BodyY, HeadTilt, ScaleX, ScaleY,
    LeftBlink, RightBlink, MouthOpen, TearShift, Spin, Dizzy,
    EyeRoundness, LeftLidDrop, RightLidDrop, KeyCount
  };
  struct Pose { float v[KeyCount]; };
  struct Point { float x, y; };
  struct Matrix { float a, b, c, d, tx, ty; };

  Arduino_Canvas* _canvas = nullptr;
  PetMotion _motion;
  float _positionX = 120, _positionY = 145;
  int16_t _drawTop = 0, _drawBottom = 279;
  PetMood _mood = PetMood::Neutral;
  Pose _pose = {}, _from = {};
  uint32_t _rng = 0x72ac43e1U;
  uint32_t _transitionAt = 0, _clipAt = 0, _nextClipAt = 0;
  uint32_t _clipDuration = 0, _lastRenderAt = 0;
  uint32_t _animationsStarted = 0;
  float _strength = 1, _sleepMix = 0;
  bool _transition = false, _clipActive = false;
  uint8_t _clipIndex = 0;
  uint8_t _bags[7][10] = {};
  uint8_t _bagPos[7] = {};
  int8_t _lastClip[7] = {};
  int8_t _requestedClip = -1;

  static Pose base(PetMood mood);
  static float clamp(float x, float lo = 0, float hi = 1);
  static float smooth(float x);
  uint32_t randomWord();
  float randomBetween(float lo, float hi);
  uint8_t pullClip();
  void startClip(uint32_t now);
  Pose compose(uint32_t now);
  static Point transform(const Matrix& matrix, float x, float y);
  static Matrix combine(const Matrix& outer, const Matrix& inner);
  static uint16_t shade(float opacity);
  void polygon(const Matrix& matrix, const Point* points, uint8_t count,
               uint16_t color);
  void eyePolygon(const Matrix& matrix, const Point* points, uint8_t count,
                  float lidY, uint16_t color);
  void ellipse(const Matrix& matrix, float x, float y, float rx, float ry,
               uint16_t color);
  void stroke(const Matrix& matrix, const Point* points, uint8_t count,
              float width, uint16_t color);
  void eye(const Matrix& head, float x, int8_t side, const Pose& pose);
  Matrix movingHead(const Pose& pose);
  void draw(const Pose& pose);
};
