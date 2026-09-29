#include "SagePet.h"
#include <math.h>

void SagePet::begin(Arduino_Canvas* canvas, uint32_t now) {
  _canvas = canvas;
  _motion.begin(now, micros() | 1);
  _motion.setBounds(40, 48);
  _mood = PetMood::Neutral;
  _wasSleeping = false;
  _ready = SageAssets::decodeFrame(SageAssets::ClipId::Calm, 0,
                                 _indices, sizeof(_indices));
  start(SageAssets::ClipId::Calm, now);
}

void SagePet::start(SageAssets::ClipId clip, uint32_t now) {
  _clip = clip;
  _clipAt = now;
  _decoded = -1;
  ++_starts;
}

void SagePet::choose(uint32_t now) {
  using C = SageAssets::ClipId;
  C next = C::Calm;
  switch (_mood) {
    case PetMood::Neutral: next = random(4) == 0 ? C::Wink : C::Calm; break;
    case PetMood::Happy: {
      const C choices[] = {C::Happy, C::Laugh, C::Wink};
      next = choices[random(3)]; break;
    }
    case PetMood::Sad: next = C::Sad; break;
    case PetMood::Surprised: next = C::Surprise; break;
    case PetMood::Sleepy: next = C::Sleepy; break;
    case PetMood::Annoyed: next = C::Angry; break;
    // No dizzy portrait was supplied: reuse surprised artwork, same fast-to-slow motion.
    case PetMood::Dizzy: next = C::Surprise; break;
  }
  start(next, now);
}

void SagePet::setMood(PetMood mood, uint32_t now) {
  _mood = mood;
  choose(now);
}

void SagePet::thumbnail(int x, int y) {
  if (!_canvas || !_ready) return;
  if (!SageAssets::decodeFrame(SageAssets::ClipId::Calm, 0, _indices, sizeof(_indices))) return;
  const auto& info = SageAssets::clip(SageAssets::ClipId::Calm);
  uint16_t* fb = _canvas->getFramebuffer();
  for (int py = 0; py < 80; ++py) for (int px = 0; px < 64; ++px) {
    const uint8_t index = _indices[(py * 2) * 128 + px * 2];
    if (index && x+px >= 0 && x+px < 240 && y+py >= 0 && y+py < 280)
      fb[(y+py)*240+x+px] = info.palette[index];
  }
  _decoded = -1;
}

void SagePet::render(uint32_t now, bool sleeping) {
  if (!_canvas || !_ready) return;
  if (sleeping != _wasSleeping) {
    _wasSleeping = sleeping;
    if (sleeping) start(SageAssets::ClipId::Sleepy, now);
    else choose(now);
  }
  if (!sleeping && now - _clipAt >= SageAssets::clip(_clip).durationMs) choose(now);
  const auto& info = SageAssets::clip(_clip);
  // Sleep holds the supplied closed-eye pose rather than looping back to open eyes.
  const uint16_t frame = sleeping ? static_cast<uint16_t>(info.frameCount / 2)
      : static_cast<uint16_t>((uint64_t(now-_clipAt) * 30 / 1000) % info.frameCount);
  if (_decoded != frame) {
    if (!SageAssets::decodeFrame(_clip, frame, _indices, sizeof(_indices))) return;
    _decoded = frame;
  }
  _motion.update(now, sleeping, _mood == PetMood::Dizzy);
  const float angle = _motion.tilt();
  const float c = cosf(angle), s = sinf(angle);
  const float sx = _motion.impactScaleX();
  const float sy = _motion.impactScaleY() * (sleeping ? 1+.008f*sinf(now/850.0f) : 1);
  const float halfW = (fabsf(c*sx)*128 + fabsf(s*sy)*160)*.5f;
  const float halfH = (fabsf(s*sx)*128 + fabsf(c*sy)*160)*.5f;
  _motion.setBounds(fmaxf(0,110-halfW), fmaxf(0,130-halfH));
  _x = 120+_motion.x(); _y = 140+_motion.y();
  const int left = max(0,static_cast<int>(floorf(_x-halfW)));
  const int right = min(239,static_cast<int>(ceilf(_x+halfW)));
  _top = max(0,static_cast<int>(floorf(_y-halfH)));
  _bottom = min(279,static_cast<int>(ceilf(_y+halfH)));
  _canvas->fillScreen(0);
  uint16_t* fb = _canvas->getFramebuffer();
  // Scanline inverse transform: divide once, then only add per destination pixel.
  // Truncation equals floor after the nonnegative source-coordinate guard.
  const float ux = c/sx, uy = s/sx, vx = -s/sy, vy = c/sy;
  for (int y=_top; y<=_bottom; ++y) {
    const float dx = left+.5f-_x, dy = y+.5f-_y;
    float u = ux*dx+uy*dy+64, v = vx*dx+vy*dy+80;
    uint16_t* row = fb+y*240;
    for (int x=left; x<=right; ++x,u+=ux,v+=vx) {
      if (u<0 || u>=128 || v<0 || v>=160) continue;
      const uint8_t index = _indices[static_cast<int>(v)*128+static_cast<int>(u)];
      if (index) row[x] = info.palette[index];
    }
  }
}
