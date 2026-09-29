#pragma once

#include <stddef.h>
#include <stdint.h>

// Immutable, losslessly repacked copies of the user-adopted Sage V03 assets.
// On ESP32, const arrays remain in memory-mapped flash; no Arduino dependency.
namespace SageAssets {

constexpr uint16_t kWidth = 128;
constexpr uint16_t kHeight = 160;
constexpr size_t kFrameBytes = size_t(kWidth) * kHeight;
constexpr uint8_t kFps = 30;
constexpr uint8_t kTransparentIndex = 0;

enum class ClipId : uint8_t {
  Calm, Happy, Laugh, Wink, Surprise, Angry, Sad, Sleepy, Count
};

struct FramePatch {
  uint32_t offset;
  uint32_t length;
};

struct Clip {
  const char* name;
  uint16_t frameCount;
  uint16_t durationMs;
  const uint16_t* palette;  // 256 RGB565 entries, ordinary native uint16_t.
  const uint8_t* frame0;
  const uint8_t* patchData;
  uint32_t patchBytes;
  const FramePatch* frames;
};

extern const Clip kClips[static_cast<uint8_t>(ClipId::Count)];
extern const uint32_t kPackedPayloadBytes;

inline bool validClip(ClipId id) {
  return static_cast<uint8_t>(id) < static_cast<uint8_t>(ClipId::Count);
}

// Invalid metadata lookups safely fall back to Calm; decode rejects invalid IDs.
inline const Clip& clip(ClipId id) {
  return kClips[validClip(id) ? static_cast<uint8_t>(id) : 0];
}

}  // namespace SageAssets
