#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "PetFace.h"
#include "PetMotion.h"
#include "CatTouchModel.h"

// V02 eye motion applied to the flat black-cat artwork on a pure black canvas.
// The interface intentionally matches SagePet so the existing touch, mood,
// sleep, IMU and partial-screen transfer paths can select this skin directly.
class BlackCatPet {
 public:
  struct Rect { int16_t left, top, right, bottom; };
  void begin(Arduino_Canvas* canvas, uint32_t now);
  void setMood(PetMood mood, uint32_t now);
  void touchDown(uint16_t x, uint16_t y, uint32_t now);
  CatTouchModel::Mode touchMove(uint16_t x, uint16_t y);
  void touchUp(uint32_t now);
  void cancelTouch();
  void tapPulse(uint16_t x, uint16_t y, uint32_t now);
  void observeMotion(float x, float y, float rotation, uint32_t now) {
    _motion.observe(x, y, rotation, now);
  }
  void render(uint32_t now, bool sleeping);
  void thumbnail(int x, int y);  // 64 x 80, top-left anchored, no canvas clear

  bool ready() const { return _canvas != nullptr; }
  const char* animationName() const;
  uint32_t animationsStarted() const { return _starts; }
  float positionX() const { return _x; }
  float positionY() const { return _y; }
  float motionSpeed() const { return _motion.speed(); }
  uint32_t collisionCount() const { return _motion.collisionCount(); }
  int16_t drawTop() const { return _top; }
  int16_t drawBottom() const { return _bottom; }
  Rect eyeRect() const { return _eyeRect; }
  Rect tongueRect() const { return _tongueRect; }
  const char* touchModeName() const;
  uint32_t strokes() const { return _strokes; }
  uint32_t drags() const { return _drags; }

 private:
  struct Pose {
    float gazeX, gazeY;
    float leftLid, rightLid;
    float eyeScale, pupilScale, pupilSlit;
    float tilt, bob, tongue;
  };
  void startClip(uint32_t now);
  Pose compose(uint32_t now, bool sleeping);
  uint32_t randomWord();
  static Pose base(PetMood mood);

  Arduino_Canvas* _canvas = nullptr;
  PetMotion _motion;
  CatTouchModel _touch;
  PetMood _mood = PetMood::Neutral;
  Pose _pose = {}, _from = {};
  uint32_t _rng = 0x3a10c47fU;
  uint32_t _clipAt = 0, _transitionAt = 0, _starts = 0, _lastRenderAt = 0;
  uint32_t _tapPulseAt = 0, _strokes = 0, _drags = 0;
  uint16_t _tapX = 120, _tapY = 140;
  bool _tapPulseValid = false;
  float _dragX = 0, _dragY = 0, _dragVx = 0, _dragVy = 0;
  uint8_t _clipIndex = 0;
  uint8_t _lastClip[7] = {255, 255, 255, 255, 255, 255, 255};
  bool _wasSleeping = false;
  float _x = 120, _y = 140;
  int16_t _top = 0, _bottom = 279;
  Rect _eyeRect = {0,0,-1,-1}, _tongueRect = {0,0,-1,-1};
};
