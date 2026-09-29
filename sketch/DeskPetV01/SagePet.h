#pragma once
#include "PetFace.h"
#include "SageAnimation.h"

// Existing adopted V03 indexed artwork; motion reuses the original PetMotion.
class SagePet {
 public:
  void begin(Arduino_Canvas* canvas, uint32_t now);
  void setMood(PetMood mood, uint32_t now);
  void observeMotion(float x, float y, float rotation, uint32_t now) {
    _motion.observe(x, y, rotation, now);
  }
  void render(uint32_t now, bool sleeping);
  void thumbnail(int x, int y);
  bool ready() const { return _ready; }
  const char* animationName() const { return SageAssets::clip(_clip).name; }
  uint32_t animationsStarted() const { return _starts; }
  float positionX() const { return _x; }
  float positionY() const { return _y; }
  float motionSpeed() const { return _motion.speed(); }
  uint32_t collisionCount() const { return _motion.collisionCount(); }
  int16_t drawTop() const { return _top; }
  int16_t drawBottom() const { return _bottom; }
 private:
  void start(SageAssets::ClipId clip, uint32_t now);
  void choose(uint32_t now);
  Arduino_Canvas* _canvas = nullptr;
  PetMotion _motion;
  PetMood _mood = PetMood::Neutral;
  SageAssets::ClipId _clip = SageAssets::ClipId::Calm;
  uint8_t _indices[SageAssets::kFrameBytes] = {};
  int32_t _decoded = -1;
  uint32_t _clipAt = 0, _starts = 0;
  float _x = 120, _y = 140;
  int16_t _top = 0, _bottom = 279;
  bool _ready = false, _wasSleeping = false;
};
