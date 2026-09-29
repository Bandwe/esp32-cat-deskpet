#include "PetFace.h"
#include "AnimationCatalog.h"
#include <math.h>

namespace {
constexpr float PI_F = 3.14159265358979323846f;
constexpr uint16_t BACKGROUND = 0x0000;

// These contours never change shape; only their scale/position does. Compute
// their original float formulas once at begin(), rather than thousands of
// sinf/cosf calls per frame while drawing the two spiral eyes and stroke caps.
struct UnitPoint { float x, y; };
struct RenderGeometry {
  UnitPoint circle[24], cap[8], corners[24], mouth[33], spiral[73];
  RenderGeometry() {
    for (uint8_t i = 0; i < 24; ++i) {
      const float a = i * (2 * PI_F / 24);
      circle[i] = {cosf(a), sinf(a)};
    }
    for (uint8_t i = 0; i < 8; ++i) {
      const float a = i * (PI_F / 4);
      cap[i] = {cosf(a), sinf(a)};
    }
    for (uint8_t corner = 0; corner < 4; ++corner) {
      for (uint8_t j = 0; j < 6; ++j) {
        const float a = corner * PI_F * .5f + j * PI_F / 10;
        corners[corner * 6 + j] = {cosf(a), sinf(a)};
      }
    }
    for (uint8_t i = 0; i < 33; ++i) {
      const float a = i * (2 * PI_F / 32);
      mouth[i] = {cosf(a), sinf(a)};
    }
    for (uint8_t i = 0; i < 73; ++i) {
      const float t = i / 72.0f, a = t * PI_F * 4.5f, radius = 2 + t * 17;
      spiral[i] = {cosf(a) * radius, sinf(a) * radius};
    }
  }
};

const RenderGeometry& renderGeometry() {
  static const RenderGeometry geometry;
  return geometry;
}
}

float PetFace::clamp(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}

float PetFace::smooth(float x) {
  x = clamp(x);
  return x * x * (3.0f - 2.0f * x);
}

PetFace::Pose PetFace::base(PetMood mood) {
  // Same base geometry as the 240 x 280 browser V01 preview.
  static const float shapes[7][10] = {
    {43, 30,   0,     0,  7, 24,  0, 0,    0,   0},
    {18, 38, -19,     0, 16, 32,  0, 0,  .8f,   0},
    {29, 32,   0, -.22f, -9, 20,  7, 0,  .1f, .8f},
    {57, 39,   0,     0,  0, 10, -3, 1,    0,   0},
    {33, 35,   0,     0,  2, 15, 11, 0, .15f,   0},
    {28, 37,   0,  .34f, -5, 23,  0, 0, .35f,   0},
    {44, 36,   0,     0, -2, 20,  1, 0,  .1f,   0}
  };
  Pose result = {};
  const uint8_t index = static_cast<uint8_t>(mood);
  for (uint8_t k = 0; k < 10; ++k) result.v[k] = shapes[index][k];
  result.v[ScaleX] = result.v[ScaleY] = 1;
  result.v[MouthOpen] = mood == PetMood::Surprised ? 1 : 0;
  result.v[Dizzy] = mood == PetMood::Dizzy ? 1 : 0;
  if (mood == PetMood::Sleepy) {
    result.v[LeftLidDrop] = .46f;
    result.v[RightLidDrop] = .50f;
  }
  return result;
}

void PetFace::begin(Arduino_Canvas* canvas) {
  (void)renderGeometry();
  _canvas = canvas;
  _mood = PetMood::Neutral;
  _pose = _from = base(_mood);
  _rng ^= micros();
  if (!_rng) _rng = 1;
  _transition = _clipActive = false;
  _animationsStarted = 0;
  _sleepMix = 0;
  _lastRenderAt = millis();
  _motion.begin(_lastRenderAt, _rng);
  _nextClipAt = _lastRenderAt + 320;
  static_assert(sizeof(_bags[0]) >= PetAnimation::MaxClipsPerMood,
                "Shuffle bag must fit the largest mood catalog");
  for (uint8_t m = 0; m < PetAnimation::MoodCount; ++m) {
    _bagPos[m] = PetAnimation::clipCounts[m];
    _lastClip[m] = -1;
  }
}

void PetFace::setMood(PetMood mood, uint32_t now) {
  if (static_cast<uint8_t>(mood) > 6) mood = PetMood::Neutral;
  _from = _pose;
  _mood = mood;
  _transitionAt = now;
  _transition = true;
  _clipActive = false;
  _requestedClip = -1;
  _nextClipAt = now + 320;
}

bool PetFace::playAnimation(const char* name, uint32_t now) {
  for (uint8_t mood = 0; mood < PetAnimation::MoodCount; ++mood) {
    for (uint8_t clip = 0; clip < PetAnimation::clipCounts[mood]; ++clip) {
      if (!strcmp(name, PetAnimation::clips[mood][clip].name)) {
        setMood(static_cast<PetMood>(mood), now);
        _requestedClip = clip;
        return true;
      }
    }
  }
  return false;
}

uint32_t PetFace::randomWord() {
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

float PetFace::randomBetween(float lo, float hi) {
  return lo + (hi - lo) * (randomWord() & 0x00ffffffU) / 16777216.0f;
}

uint8_t PetFace::pullClip() {
  const uint8_t m = static_cast<uint8_t>(_mood);
  const uint8_t count = PetAnimation::clipCounts[m];
  if (_bagPos[m] >= count) {
    for (uint8_t i = 0; i < count; ++i) _bags[m][i] = i;
    for (int8_t i = count - 1; i > 0; --i) {
      const uint8_t j = randomWord() % (i + 1);
      const uint8_t temp = _bags[m][i];
      _bags[m][i] = _bags[m][j];
      _bags[m][j] = temp;
    }
    if (count > 1 && _bags[m][0] == _lastClip[m]) {
      const uint8_t temp = _bags[m][0];
      _bags[m][0] = _bags[m][1];
      _bags[m][1] = temp;
    }
    _bagPos[m] = 0;
  }
  const uint8_t result = _bags[m][_bagPos[m]++];
  _lastClip[m] = result;
  return result;
}

void PetFace::startClip(uint32_t now) {
  _clipIndex = _requestedClip >= 0 ? static_cast<uint8_t>(_requestedClip) : pullClip();
  if (_requestedClip >= 0) _lastClip[static_cast<uint8_t>(_mood)] = _clipIndex;
  _requestedClip = -1;
  _clipAt = now;
  _clipDuration = static_cast<uint32_t>(
      PetAnimation::clips[static_cast<uint8_t>(_mood)][_clipIndex].duration *
      randomBetween(.9f, 1.12f));
  _strength = randomBetween(.88f, 1.1f);
  _clipActive = true;
  ++_animationsStarted;
}

const char* PetFace::animationName() const {
  if (_sleepMix >= 1) return "sleeping";
  if (_transition) return "transition";
  if (!_clipActive) return "idle-pause";
  return PetAnimation::clips[static_cast<uint8_t>(_mood)][_clipIndex].name;
}

PetFace::Pose PetFace::compose(uint32_t now) {
  Pose p = base(_mood);
  if (_transition) {
    const float t = smooth((now - _transitionAt) / 320.0f);
    for (uint8_t k = 0; k < KeyCount; ++k)
      p.v[k] = _from.v[k] + (p.v[k] - _from.v[k]) * t;
    if (t >= 1) _transition = false;
  }
  if (_clipActive && now - _clipAt >= _clipDuration) {
    _clipActive = false;
    const bool sleepy = _mood == PetMood::Sleepy;
    _nextClipAt = now + static_cast<uint32_t>(
        randomBetween(sleepy ? 650 : 400, sleepy ? 1700 : 1100));
  }
  if (!_transition && !_clipActive &&
      static_cast<int32_t>(now - _nextClipAt) >= 0) startClip(now);

  float v[PetAnimation::MotionCount] = {};
  if (_clipActive) {
    const PetAnimation::Clip& clip =
        PetAnimation::clips[static_cast<uint8_t>(_mood)][_clipIndex];
    const float progress = clamp((now - _clipAt) / static_cast<float>(_clipDuration));
    uint8_t i = 1;
    while (i < clip.frameCount - 1 && progress > clip.frames[i].at) ++i;
    const PetAnimation::Frame& a = clip.frames[i - 1];
    const PetAnimation::Frame& b = clip.frames[i];
    const float t = smooth((progress - a.at) / (b.at - a.at));
    for (uint8_t k = 0; k < PetAnimation::MotionCount; ++k) {
      const float strength =
          (k == PetAnimation::LeftBlink || k == PetAnimation::RightBlink ||
           k == PetAnimation::EyeRoundness || k == PetAnimation::LeftLidDrop ||
           k == PetAnimation::RightLidDrop) ? 1 : _strength;
      v[k] = (a.value[k] + (b.value[k] - a.value[k]) * t) * strength;
    }
  }
  p.v[GazeX] += v[PetAnimation::GazeX];
  p.v[GazeY] += v[PetAnimation::GazeY];
  p.v[BodyX] += v[PetAnimation::BodyX] + v[PetAnimation::Shake] * sinf(now / 53.0f);
  p.v[BodyY] += v[PetAnimation::BodyY];
  p.v[HeadTilt] += v[PetAnimation::HeadTilt];
  p.v[ScaleX] *= fmaxf(.45f, 1 + v[PetAnimation::EyeScaleX]);
  p.v[ScaleY] *= fmaxf(.06f, 1 + v[PetAnimation::EyeScaleY]);
  p.v[LeftBlink] = clamp(p.v[LeftBlink] + v[PetAnimation::LeftBlink]);
  p.v[RightBlink] = clamp(p.v[RightBlink] + v[PetAnimation::RightBlink]);
  p.v[EyeRoundness] = clamp(p.v[EyeRoundness] + v[PetAnimation::EyeRoundness]);
  p.v[LeftLidDrop] = clamp(p.v[LeftLidDrop] + v[PetAnimation::LeftLidDrop], 0, .94f);
  p.v[RightLidDrop] = clamp(p.v[RightLidDrop] + v[PetAnimation::RightLidDrop], 0, .94f);
  p.v[Curve] += 40 * v[PetAnimation::CurveDelta];
  p.v[Mouth] += 30 * v[PetAnimation::MouthCurveDelta];
  p.v[MouthWidth] = fmaxf(6, p.v[MouthWidth] + v[PetAnimation::MouthWidthDelta]);
  p.v[MouthOpen] = clamp(p.v[MouthOpen] + v[PetAnimation::MouthOpen]);
  p.v[Blush] = clamp(p.v[Blush] + v[PetAnimation::BlushDelta]);
  p.v[TearShift] += v[PetAnimation::TearShift];
  p.v[Spin] += v[PetAnimation::Spin];
  const bool sleepy = _mood == PetMood::Sleepy;
  const float breathing = sinf(now / (sleepy ? 850.0f : 650.0f));
  p.v[BodyY] += breathing * (sleepy ? 1.7f : .7f);
  p.v[ScaleY] *= 1 + breathing * .014f * (1 - p.v[EyeRoundness]);
  if (p.v[Dizzy] > .01f) {
    p.v[BodyX] += sinf(now / 270.0f) * 3 * p.v[Dizzy];
    p.v[HeadTilt] += sinf(now / 410.0f) * .045f * p.v[Dizzy];
    p.v[Spin] += now / 420.0f;
  }
  return p;
}

PetFace::Point PetFace::transform(const Matrix& m, float x, float y) {
  return {m.a * x + m.c * y + m.tx, m.b * x + m.d * y + m.ty};
}

PetFace::Matrix PetFace::combine(const Matrix& o, const Matrix& i) {
  return {o.a * i.a + o.c * i.b, o.b * i.a + o.d * i.b,
          o.a * i.c + o.c * i.d, o.b * i.c + o.d * i.d,
          o.a * i.tx + o.c * i.ty + o.tx,
          o.b * i.tx + o.d * i.ty + o.ty};
}

uint16_t PetFace::shade(float opacity) {
  opacity = clamp(opacity);
  // White on pure black; fades stay grayscale with no cyan tint.
  const uint8_t r = static_cast<uint8_t>(255 * opacity);
  const uint8_t g = r;
  const uint8_t b = r;
  return ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);
}

void PetFace::polygon(const Matrix& matrix, const Point* points, uint8_t count,
                      uint16_t color) {
  // All callers supply convex contours (strokes are split into convex quads
  // and caps). Keep subpixel coordinates until rasterization, rather than
  // rounding vertices and triangulating: that caused one-pixel edge jumps.
  constexpr uint8_t MAX_VERTICES = 64;
  constexpr uint8_t SAMPLES = 4;
  if (!_canvas || !points || count < 3 || count > MAX_VERTICES) return;
  uint16_t* framebuffer = _canvas->getFramebuffer();
  if (!framebuffer) return;

  const uint8_t rotation = _canvas->getRotation() & 3;
  const int16_t width = (rotation & 1) ? _canvas->height() : _canvas->width();
  const int16_t height = (rotation & 1) ? _canvas->width() : _canvas->height();
  if (width <= 0 || height <= 0) return;
  const auto devicePoint = [&](const Point& point) -> Point {
    const Point p = transform(matrix, point.x, point.y);
    // Transform continuous pixel-cell coordinates into the raw framebuffer.
    // The deployed 240 x 280 canvas uses rotation 0; the other rotations retain
    // Arduino_Canvas's physical row stride without out-of-bounds writes.
    switch (rotation) {
      case 1: return {width - p.y, p.x};
      case 2: return {width - p.x, height - p.y};
      case 3: return {p.y, height - p.x};
      default: return p;
    }
  };

  struct Edge { float lowY, highY, lowX, dxPerY; };
  Edge edges[MAX_VERTICES];
  uint8_t edgeCount = 0;
  Point previous = devicePoint(points[count - 1]);
  if (!isfinite(previous.x) || !isfinite(previous.y)) return;
  float minY = previous.y, maxY = previous.y;
  for (uint8_t i = 0; i < count; ++i) {
    const Point point = devicePoint(points[i]);
    if (!isfinite(point.x) || !isfinite(point.y)) return;
    if (point.y < minY) minY = point.y;
    if (point.y > maxY) maxY = point.y;
    if (point.y != previous.y) {
      const Point& low = point.y < previous.y ? point : previous;
      const Point& high = point.y < previous.y ? previous : point;
      const Edge edge = {low.y, high.y, low.x,
                         (high.x - low.x) / (high.y - low.y)};
      // Tiny convex contours (usually 4 or 8 edges) sort cheaply, then each
      // edge enters/leaves the active set only once over the entire polygon.
      uint8_t at = edgeCount;
      while (at && edge.lowY < edges[at - 1].lowY) {
        edges[at] = edges[at - 1];
        --at;
      }
      edges[at] = edge;
      ++edgeCount;
    }
    previous = point;
  }
  if (edgeCount < 2 || maxY <= 0 || minY >= height) return;
  const int16_t firstRow = static_cast<int16_t>(floorf(fmaxf(0, minY)));
  const int16_t endRow = static_cast<int16_t>(ceilf(fminf(height, maxY)));
  const uint32_t sourceR = (color >> 11) & 31;
  const uint32_t sourceG = (color >> 5) & 63;
  const uint32_t sourceB = color & 31;
  uint8_t active[MAX_VERTICES], activeCount = 0, nextEdge = 0;

  for (int16_t y = firstRow; y < endRow; ++y) {
    float left[SAMPLES], right[SAMPLES];
    float outsideLeft = width, outsideRight = 0;
    float insideLeft = 0, insideRight = width;
    uint8_t coveredSamples = 0;
    for (uint8_t sample = 0; sample < SAMPLES; ++sample) {
      const float sampleY = y + (sample + .5f) / SAMPLES;
      float spanLeft = width, spanRight = 0;
      uint8_t retained = 0;
      for (uint8_t e = 0; e < activeCount; ++e) {
        if (sampleY < edges[active[e]].highY) active[retained++] = active[e];
      }
      activeCount = retained;
      while (nextEdge < edgeCount && edges[nextEdge].lowY <= sampleY) {
        // Half-open intervals still count shared vertices only once. Edges
        // wholly above the first clipped row are skipped, not activated.
        if (sampleY < edges[nextEdge].highY) active[activeCount++] = nextEdge;
        ++nextEdge;
      }
      for (uint8_t e = 0; e < activeCount; ++e) {
        const Edge& edge = edges[active[e]];
        const float x = edge.lowX + (sampleY - edge.lowY) * edge.dxPerY;
        if (x < spanLeft) spanLeft = x;
        if (x > spanRight) spanRight = x;
      }
      spanLeft = clamp(spanLeft, 0, width);
      spanRight = clamp(spanRight, 0, width);
      if (!activeCount || spanRight <= spanLeft) {
        left[sample] = right[sample] = 0;
        continue;
      }
      left[sample] = spanLeft;
      right[sample] = spanRight;
      if (spanLeft < outsideLeft) outsideLeft = spanLeft;
      if (spanRight > outsideRight) outsideRight = spanRight;
      if (spanLeft > insideLeft) insideLeft = spanLeft;
      if (spanRight < insideRight) insideRight = spanRight;
      ++coveredSamples;
    }
    if (!coveredSamples) continue;
    const int16_t firstX = static_cast<int16_t>(floorf(outsideLeft));
    const int16_t endX = static_cast<int16_t>(ceilf(outsideRight));
    uint16_t* row = framebuffer + static_cast<int32_t>(y) * width;
    const auto blendEdge = [&](int16_t x) {
      if (row[x] == color) return;
      float coverage = 0;
      for (uint8_t sample = 0; sample < SAMPLES; ++sample) {
        const float sampleRight = x + 1.0f < right[sample] ? x + 1.0f : right[sample];
        const float sampleLeft = x > left[sample] ? static_cast<float>(x) : left[sample];
        coverage += clamp(sampleRight - sampleLeft);
      }
      const uint32_t alpha = static_cast<uint32_t>(coverage * (256 / SAMPLES) + .5f);
      if (!alpha) return;
      if (alpha >= 256) { row[x] = color; return; }
      // Blend against the existing destination, not assumed black. This is
      // essential for dark inner-eye cutouts and overlapping curved strokes.
      const uint16_t destination = row[x];
      const uint32_t inverse = 256 - alpha;
      const uint32_t r = (sourceR * alpha + ((destination >> 11) & 31) * inverse + 128) >> 8;
      const uint32_t g = (sourceG * alpha + ((destination >> 5) & 63) * inverse + 128) >> 8;
      const uint32_t b = (sourceB * alpha + (destination & 31) * inverse + 128) >> 8;
      row[x] = static_cast<uint16_t>((r << 11) | (g << 5) | b);
    };

    // The intersection of all four spans is fully covered: fill that common
    // interior directly. Only boundary pixels need four coverage evaluations.
    int16_t fullStart = static_cast<int16_t>(ceilf(insideLeft));
    int16_t fullEnd = static_cast<int16_t>(floorf(insideRight));
    if (coveredSamples != SAMPLES || fullStart >= fullEnd) {
      for (int16_t x = firstX; x < endX; ++x) blendEdge(x);
    } else {
      for (int16_t x = firstX; x < fullStart; ++x) blendEdge(x);
      for (int16_t x = fullStart; x < fullEnd; ++x) row[x] = color;
      for (int16_t x = fullEnd; x < endX; ++x) blendEdge(x);
    }
  }
}

void PetFace::ellipse(const Matrix& matrix, float x, float y, float rx, float ry,
                      uint16_t color) {
  const UnitPoint* circle = renderGeometry().circle;
  Point points[24];
  for (uint8_t i = 0; i < 24; ++i) {
    points[i] = {x + circle[i].x * rx, y + circle[i].y * ry};
  }
  polygon(matrix, points, 24, color);
}

void PetFace::stroke(const Matrix& matrix, const Point* points, uint8_t count,
                     float width, uint16_t color) {
  const float radius = width * .5f;
  const UnitPoint* capUnit = renderGeometry().cap;
  Point capOffset[8];
  for (uint8_t j = 0; j < 8; ++j)
    capOffset[j] = {capUnit[j].x * radius, capUnit[j].y * radius};
  for (uint8_t i = 1; i < count; ++i) {
    const Point a = points[i - 1], b = points[i];
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = sqrtf(dx * dx + dy * dy);
    if (length < .001f) continue;
    const float nx = -dy * radius / length, ny = dx * radius / length;
    const Point quad[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny},
                           {b.x - nx, b.y - ny}, {a.x - nx, a.y - ny}};
    polygon(matrix, quad, 4, color);
    // Small eight-sided caps keep bent strokes joined and round without heap use.
    Point cap[8];
    for (uint8_t j = 0; j < 8; ++j) {
      cap[j] = {a.x + capOffset[j].x, a.y + capOffset[j].y};
    }
    polygon(matrix, cap, 8, color);
  }
  if (count) ellipse(matrix, points[count - 1].x, points[count - 1].y,
                     radius, radius, color);
}

void PetFace::eyePolygon(const Matrix& matrix, const Point* points, uint8_t count,
                         float lidY, uint16_t color) {
  // Clip the convex eye contour before rasterization. Moving the top eyelid
  // down changes the opening, rather than shrinking the entire eye in place.
  if (!count || count > 24) return;
  Point clipped[28];
  uint8_t clippedCount = 0;
  Point previous = points[count - 1];
  bool previousInside = previous.y >= lidY;
  for (uint8_t i = 0; i < count; ++i) {
    const Point current = points[i];
    const bool inside = current.y >= lidY;
    if (inside != previousInside) {
      const float t = (lidY - previous.y) / (current.y - previous.y);
      clipped[clippedCount++] = {previous.x + t * (current.x - previous.x), lidY};
    }
    if (inside) clipped[clippedCount++] = current;
    previous = current;
    previousInside = inside;
  }
  polygon(matrix, clipped, clippedCount, color);
}

void PetFace::eye(const Matrix& head, float x, int8_t side, const Pose& p) {
  const float blink = p.v[side == 1 ? LeftBlink : RightBlink];
  const float sx = p.v[ScaleX], sy = p.v[ScaleY] * fmaxf(.045f, 1 - blink);
  const float angle = p.v[Tilt] * side, c = cosf(angle), s = sinf(angle);
  const Matrix local = {c * sx, s * sx, -s * sy, c * sy,
                         x + p.v[GazeX], 124 + p.v[GazeY]};
  const Matrix matrix = combine(head, local);
  const float roundness = smooth(p.v[EyeRoundness]);
  const float w = p.v[Width] + (48 - p.v[Width]) * roundness;
  const float h = p.v[Height] + (48 - p.v[Height]) * roundness;
  const float lidDrop = p.v[side == 1 ? LeftLidDrop : RightLidDrop];
  const float lidY = -h * .5f + h * lidDrop;
  const float lineMix = smooth(fabsf(p.v[Curve]) / 9);
  const float normalAlpha = (1 - lineMix) * (1 - p.v[Dizzy]);
  if (normalAlpha > .01f) {
    const float halfMinimum = fminf(w, h) * .5f;
    const float ordinaryRadius = fminf(halfMinimum, 14);
    const float radius = ordinaryRadius + (halfMinimum - ordinaryRadius) * roundness;
    const UnitPoint* corners = renderGeometry().corners;
    Point outline[24];
    for (uint8_t corner = 0; corner < 4; ++corner) {
      const float cx = (corner == 0 || corner == 3) ? w * .5f - radius : -w * .5f + radius;
      const float cy = corner < 2 ? h * .5f - radius : -h * .5f + radius;
      for (uint8_t j = 0; j < 6; ++j) {
        const UnitPoint& unit = corners[corner * 6 + j];
        outline[corner * 6 + j] = {cx + unit.x * radius, cy + unit.y * radius};
      }
    }
    eyePolygon(matrix, outline, 24, lidY, shade(normalAlpha));
    if (p.v[Round] > .01f) {
      Point inner[24];
      const UnitPoint* circle = renderGeometry().circle;
      for (uint8_t i = 0; i < 24; ++i)
        inner[i] = {circle[i].x * w * .24f, circle[i].y * h * .30f};
      eyePolygon(matrix, inner, 24, lidY, shade(normalAlpha * (1 - p.v[Round])));
    }
  }
  const float curveAlpha = lineMix * (1 - p.v[Dizzy]);
  if (curveAlpha > .01f) {
    Point points[21];
    for (uint8_t i = 0; i < 21; ++i) {
      const float t = i / 20.0f, u = 1 - t;
      points[i] = {-w * .5f + w * t, 3 * (u * u + t * t) + 2 * u * t * p.v[Curve]};
    }
    stroke(matrix, points, 21, 8, shade(curveAlpha));
  }
  if (p.v[Dizzy] > .01f) {
    const float c2 = cosf(p.v[Spin] * side), s2 = sinf(p.v[Spin] * side);
    const Matrix rotation = {c2, s2, -s2, c2, 0, 0};
    const UnitPoint* spiralShape = renderGeometry().spiral;
    Point spiral[73];
    for (uint8_t i = 0; i < 73; ++i) {
      spiral[i] = {spiralShape[i].x, spiralShape[i].y};
    }
    stroke(combine(matrix, rotation), spiral, 73, 3.4f, shade(p.v[Dizzy]));
  }
}

PetFace::Matrix PetFace::movingHead(const Pose& p) {
  // Uniformly reduce the original face to leave room for screen-wide travel.
  // Collision squash is layered on top; the individual feature shapes remain.
  const float angle = p.v[HeadTilt] + _motion.tilt();
  const float c = cosf(angle), s = sinf(angle);
  const float sx = .80f * _motion.impactScaleX();
  const float sy = .80f * _motion.impactScaleY();
  Matrix head = {c * sx, s * sx, -s * sy, c * sy, 0, 0};
  float minX = 1000, minY = 1000, maxX = -1000, maxY = -1000;
  const auto bounds = [&](float x, float y, float halfW, float halfH) {
    for (int8_t i = -1; i <= 1; i += 2) {
      for (int8_t j = -1; j <= 1; j += 2) {
        const float dx = x + i * halfW - 120, dy = y + j * halfH - 145;
        const float px = head.a * dx + head.c * dy;
        const float py = head.b * dx + head.d * dy;
        minX = fminf(minX, px); maxX = fmaxf(maxX, px);
        minY = fminf(minY, py); maxY = fmaxf(maxY, py);
      }
    }
  };
  // Conservative analytic bounds avoid a costly extra drawing pass. Include
  // curved-eye stroke caps, rotating spirals, mouth, blush and the moving tear.
  for (int8_t side = -1; side <= 1; side += 2) {
    const float roundness = smooth(p.v[EyeRoundness]);
    const float eyeWidth = p.v[Width] + (48 - p.v[Width]) * roundness;
    const float eyeHeight = p.v[Height] + (48 - p.v[Height]) * roundness;
    const float blink = p.v[side == 1 ? LeftBlink : RightBlink];
    const float eyeSx = p.v[ScaleX];
    const float eyeSy = p.v[ScaleY] * fmaxf(.045f, 1 - blink);
    const float halfW = fmaxf(eyeWidth * .5f + 4, p.v[Dizzy] > .01f ? 21 : 0);
    const float halfH = fmaxf(fmaxf(eyeHeight * .5f + 4,
                                  fabsf(p.v[Curve]) * .5f + 7),
                            p.v[Dizzy] > .01f ? 21 : 0);
    const float ec = fabsf(cosf(p.v[Tilt] * side)), es = fabsf(sinf(p.v[Tilt] * side));
    bounds((side == 1 ? 74 : 166) + p.v[GazeX], 124 + p.v[GazeY],
           ec * eyeSx * halfW + es * eyeSy * halfH,
           es * eyeSx * halfW + ec * eyeSy * halfH);
  }
  bounds(120, 184, fmaxf(8, p.v[MouthWidth] * .5f + 3),
         fmaxf(19, fabsf(p.v[Mouth]) * .5f + 6));
  if (p.v[Blush] > .01f) { bounds(50, 168, 12, 4); bounds(190, 168, 12, 4); }
  if (p.v[Tear] > .01f) bounds(181, 165 + p.v[TearShift], 10, 11);
  // Keep a small inset for the LCD's rounded physical corners.
  constexpr float inset = 10;
  const float lowX = inset - 120 - minX - p.v[BodyX];
  const float highX = 240 - inset - 120 - maxX - p.v[BodyX];
  const float lowY = inset - 145 - minY - p.v[BodyY];
  const float highY = 280 - inset - 145 - maxY - p.v[BodyY];
  _motion.setBounds(fmaxf(0, fminf(-lowX, highX)), fmaxf(0, fminf(-lowY, highY)));
  const float offsetX = clamp(_motion.x(), lowX, highX);
  const float offsetY = clamp(_motion.y(), lowY, highY);
  _positionX = 120 + p.v[BodyX] + offsetX;
  _positionY = 145 + p.v[BodyY] + offsetY;
  _drawTop = static_cast<int16_t>(fmaxf(0, floorf(_positionY + minY) - 2));
  _drawBottom = static_cast<int16_t>(fminf(279, ceilf(_positionY + maxY) + 2));
  head.tx = _positionX - 120 * head.a - 145 * head.c;
  head.ty = _positionY - 120 * head.b - 145 * head.d;
  return head;
}

void PetFace::draw(const Pose& p) {
  _canvas->fillScreen(BACKGROUND);
  const Matrix head = movingHead(p);
  eye(head, 74, 1, p);
  eye(head, 166, -1, p);
  if (p.v[MouthOpen] < .99f) {
    Point points[21];
    for (uint8_t i = 0; i < 21; ++i) {
      const float t = i / 20.0f;
      points[i] = {120 - p.v[MouthWidth] * .5f + p.v[MouthWidth] * t,
                    181 + 2 * (1 - t) * t * p.v[Mouth]};
    }
    stroke(head, points, 21, 5, shade(1 - p.v[MouthOpen]));
  }
  if (p.v[MouthOpen] > .01f) {
    const UnitPoint* mouth = renderGeometry().mouth;
    const float rx = fmaxf(5, p.v[MouthWidth] * .4f), ry = 5 + 9 * p.v[MouthOpen];
    Point points[33];
    for (uint8_t i = 0; i < 33; ++i) {
      points[i] = {120 + mouth[i].x * rx, 185 + mouth[i].y * ry};
    }
    stroke(head, points, 33, 5, shade(p.v[MouthOpen]));
  }
  if (p.v[Blush] > .01f) {
    ellipse(head, 50, 168, 12, 4, shade(p.v[Blush] * .43f));
    ellipse(head, 190, 168, 12, 4, shade(p.v[Blush] * .43f));
  }
  if (p.v[Tear] > .01f) {
    // The two cubic sides of the preview's little tear form a convex droplet.
    Point points[22];
    for (uint8_t side = 0; side < 2; ++side) {
      for (uint8_t i = 0; i < 11; ++i) {
        const float t = i / 10.0f, u = 1 - t;
        const float x0 = 181, x1 = side == 0 ? 177 : 191;
        const float x2 = side == 0 ? 171 : 185, x3 = 181;
        const float y0 = side == 0 ? 154 : 176, y1 = side == 0 ? 162 : 174;
        const float y2 = side == 0 ? 174 : 162, y3 = side == 0 ? 176 : 154;
        points[side * 11 + i] = {
          u*u*u*x0 + 3*u*u*t*x1 + 3*u*t*t*x2 + t*t*t*x3,
          u*u*u*y0 + 3*u*u*t*y1 + 3*u*t*t*y2 + t*t*t*y3 + p.v[TearShift]
        };
      }
    }
    polygon(head, points, 22, shade(p.v[Tear]));
  }
}

void PetFace::render(uint32_t now, bool sleeping) {
  if (!_canvas) return;
  _motion.update(now, sleeping, _mood == PetMood::Dizzy);
  const float step = fminf(static_cast<float>(now - _lastRenderAt), 80) / 320.0f;
  _lastRenderAt = now;
  _sleepMix = clamp(_sleepMix + (sleeping ? step : -step));
  _pose = compose(now);
  Pose visible = _pose;
  if (_sleepMix > 0) {
    Pose asleep = base(PetMood::Sleepy);
    asleep.v[Curve] = 9;
    asleep.v[BodyY] = sinf(now / 850.0f) * 1.7f;
    asleep.v[HeadTilt] = sinf(now / 1600.0f) * .015f;
    const float blend = smooth(_sleepMix);
    for (uint8_t k = 0; k < KeyCount; ++k)
      visible.v[k] += (asleep.v[k] - visible.v[k]) * blend;
  }
  draw(visible);
}
