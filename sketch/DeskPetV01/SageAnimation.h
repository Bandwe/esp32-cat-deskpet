#pragma once

#include <string.h>
#include "SageAssets.h"

namespace SageAssets {

// Random-access frame decode into caller-owned memory. Each patch is independent
// of previous frames: copy frame0, then apply [LE u16 position, LE u16 length,
// literal bytes] records. Identical frames share patch descriptors in flash.
// Returns false for invalid clip/frame/capacity or malformed compiled-in data.
inline bool decodeFrame(ClipId id, uint16_t frame, uint8_t* out,
                        size_t capacity = kFrameBytes) {
  if (!validClip(id) || !out || capacity < kFrameBytes) return false;
  const Clip& asset = clip(id);
  if (frame >= asset.frameCount) return false;
  const FramePatch& patch = asset.frames[frame];
  if (patch.offset > asset.patchBytes ||
      patch.length > asset.patchBytes - patch.offset) return false;
  memcpy(out, asset.frame0, kFrameBytes);
  const uint8_t* cursor = asset.patchData + patch.offset;
  const uint8_t* end = cursor + patch.length;
  while (cursor != end) {
    if (size_t(end - cursor) < 4) return false;
    const uint16_t position = uint16_t(cursor[0]) | (uint16_t(cursor[1]) << 8);
    const uint16_t length = uint16_t(cursor[2]) | (uint16_t(cursor[3]) << 8);
    cursor += 4;
    if (!length || position > kFrameBytes ||
        length > kFrameBytes - position || size_t(end - cursor) < length)
      return false;
    memcpy(out + position, cursor, length);
    cursor += length;
  }
  return true;
}

// Clip-local elapsed milliseconds. Late frames are skipped, not queued.
inline uint16_t frameAt(ClipId id, uint32_t elapsedMs) {
  const Clip& asset = clip(id);
  return uint16_t((uint64_t(elapsedMs) * kFps / 1000) % asset.frameCount);
}

}  // namespace SageAssets
