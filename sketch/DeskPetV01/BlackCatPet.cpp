#include "BlackCatPet.h"
#include "CatTouchDecay.h"
#include <math.h>

namespace {
// These are the same flat V06 colours. A single coverage ramp only smooths
// the edge at LCD resolution; it does not introduce texture or a gradient.
constexpr uint16_t kGold = 0xfecf;   // #FFDA7B
constexpr uint16_t kPink = 0xecd4;   // #EF98A1
constexpr uint16_t kWhite = 0xffff;
constexpr float kEyeSeparation = 52.0f;
constexpr int kSpiralSize = 48;
constexpr float kSpiralCenter = (kSpiralSize - 1) * .5f;
constexpr float kSpiralPixelsPerTexel = 19.5f / (kSpiralSize * .5f);
uint8_t spiralMask[kSpiralSize * kSpiralSize] = {};
bool spiralMaskReady = false;

enum Channel : uint8_t {
  GazeX, GazeY, LeftLid, RightLid, EyeScale, PupilScale,
  Tilt, Bob, Tongue, PupilSlit, ChannelCount
};
struct Key { float at; float value[ChannelCount]; };
struct Clip { const char* name; uint16_t durationMs; const Key* keys; uint8_t count; };
struct Catalog { const Clip* clips; uint8_t count; };

// Every key is an offset from the mood pose. Keys are independent: no stale
// tongue, wink or gaze value can leak into the next animation.
#define K(t, gx, gy, ll, rl, es, ps, tilt, bob, tongue) \
  {t, {gx, gy, ll, rl, es, ps, tilt, bob, tongue, 0}}
#define KS(t, gx, gy, ll, rl, es, ps, tilt, bob, tongue, slit) \
  {t, {gx, gy, ll, rl, es, ps, tilt, bob, tongue, slit}}
#define CLIP(name, duration, keys) \
  {name, duration, keys, static_cast<uint8_t>(sizeof(keys) / sizeof(keys[0]))}

const Key neutralBlink[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.30f,0,0,0,0,0,0,0,0,0),
  K(.40f,0,0,1,1,0,0,0,0,0), K(.52f,0,0,1,1,0,0,0,0,0),
  K(.66f,0,0,0,0,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Key neutralLeft[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.09f,-7,-2,.06f,.12f,0,0,-.025f,0,0),
  KS(.18f,-6,-1,.04f,.10f,0,0,-.02f,0,0,.18f),
  KS(.42f,-6,-1,.04f,.10f,0,0,-.02f,0,0,.78f),
  KS(.68f,-6,-1,.04f,.10f,0,0,-.02f,0,0,.78f),
  KS(.84f,-2,0,.45f,.45f,0,0,0,0,0,.20f), K(1,0,0,0,0,0,0,0,0,0)
};
const Key neutralRight[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.09f,7,-2,.12f,.06f,0,0,.025f,0,0),
  KS(.18f,6,-1,.10f,.04f,0,0,.02f,0,0,.18f),
  KS(.42f,6,-1,.10f,.04f,0,0,.02f,0,0,.78f),
  KS(.68f,6,-1,.10f,.04f,0,0,.02f,0,0,.78f),
  KS(.84f,2,0,.45f,.45f,0,0,0,0,0,.20f), K(1,0,0,0,0,0,0,0,0,0)
};
const Key neutralDoubleBlink[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.20f,0,0,1,1,0,0,0,0,0),
  K(.33f,0,0,0,0,0,0,0,0,0), K(.47f,0,0,1,1,0,0,0,0,0),
  K(.60f,0,0,0,0,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Key neutralWide[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.22f,0,-2,0,0,.16f,.10f,0,-1,0),
  K(.68f,0,-2,0,0,.16f,.10f,0,-1,0),
  K(.84f,0,0,.30f,.30f,.06f,.03f,0,0,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key neutralSquint[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.24f,0,0,.64f,.68f,0,0,0,0,0),
  K(.62f,0,0,.66f,.70f,0,0,0,0,0),
  K(.84f,0,0,.12f,.12f,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Key neutralPeek[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.22f,0,2,.70f,.76f,0,0,-.02f,3,0),
  K(.42f,-11,0,.76f,.30f,0,0,-.025f,3,0),
  K(.62f,-11,0,.76f,.30f,0,0,-.025f,3,0),
  K(.78f,11,0,.35f,.72f,0,0,.02f,1,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Clip neutralClips[] = {
  CLIP("cat-soft-blink",1250,neutralBlink),
  CLIP("cat-look-left",1900,neutralLeft),
  CLIP("cat-look-right",1900,neutralRight),
  CLIP("cat-double-blink",1450,neutralDoubleBlink),
  CLIP("cat-wide-curious",1750,neutralWide),
  CLIP("cat-squint",1800,neutralSquint),
  CLIP("cat-sleepy-peek",2300,neutralPeek),
};

const Key happyBlep[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.12f,0,0,0,0,0,0,0,0,0),
  K(.20f,0,1,.06f,.06f,0,.04f,0,-1,.75f),
  K(.28f,0,2,.10f,.10f,0,.08f,0,-1,1),
  K(.38f,1,2,.10f,.10f,0,.08f,0,-1,1),
  K(.47f,0,2,.10f,.10f,0,.08f,0,-1,1),
  K(.57f,0,1,.08f,.08f,0,.04f,0,-1,.88f),
  K(.68f,0,0,0,0,0,0,0,0,.18f),
  K(.73f,0,0,0,0,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Key happyWinkBlep[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.12f,0,0,0,0,0,0,0,0,0),
  K(.20f,1,1,.78f,0,0,.03f,-.025f,0,.75f),
  K(.28f,1,2,1,0,0,.06f,-.035f,0,1),
  K(.38f,1,2,1,0,0,.06f,-.035f,0,1),
  K(.47f,1,2,1,0,0,.06f,-.035f,0,1),
  K(.57f,0,1,.68f,0,0,.03f,-.02f,0,.88f),
  K(.68f,0,0,.28f,0,0,0,0,0,.18f),
  K(.73f,0,0,0,0,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Key happySqueeze[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.23f,0,0,.56f,.56f,.03f,0,0,-2,0),
  K(.62f,0,0,.50f,.50f,.03f,0,0,-2,0),
  K(.84f,0,0,.15f,.15f,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Key happyBounce[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.20f,2,0,0,0,.10f,-.02f,.03f,-5,0),
  K(.43f,-2,0,.15f,.15f,.02f,0,-.03f,2,0),
  K(.62f,1,0,0,0,.07f,0,.02f,-3,0),
  K(.82f,0,0,.22f,.22f,0,0,0,1,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Clip happyClips[] = {
  CLIP("cat-blep",2400,happyBlep),
  CLIP("cat-wink-blep",2400,happyWinkBlep),
  CLIP("cat-happy-squint",1650,happySqueeze),
  CLIP("cat-happy-bounce",1750,happyBounce),
};

const Key sadDroop[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.30f,0,3,.22f,.27f,-.05f,0,-.025f,3,0),
  K(.72f,0,3,.22f,.27f,-.05f,0,-.025f,3,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key sadAvert[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.20f,-5,1,.08f,.12f,0,0,-.025f,1,0),
  K(.75f,-5,1,.08f,.12f,0,0,-.025f,1,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key sadSlowBlink[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.31f,0,2,.52f,.52f,0,0,0,2,0),
  K(.55f,0,2,.52f,.52f,0,0,0,2,0),
  K(.82f,0,0,0,0,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Clip sadClips[] = {
  CLIP("cat-sad-droop",2200,sadDroop),
  CLIP("cat-sad-avert",2100,sadAvert),
  CLIP("cat-sad-blink",2300,sadSlowBlink),
};

const Key surpriseWide[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.17f,0,-2,0,0,.14f,.05f,0,-2,0),
  K(.73f,0,-2,0,0,.14f,.05f,0,-2,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key surpriseJump[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.16f,0,-2,0,0,.18f,.07f,-.03f,-6,0),
  K(.39f,0,0,0,0,.08f,0,.02f,3,0),
  K(.68f,0,-1,0,0,.13f,.04f,0,-1,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key surpriseFreeze[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.19f,4,-1,0,0,.10f,.04f,.015f,-2,0),
  K(.79f,4,-1,0,0,.10f,.04f,.015f,-2,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Clip surpriseClips[] = {
  CLIP("cat-wide-eyed",1850,surpriseWide),
  CLIP("cat-startled",1700,surpriseJump),
  CLIP("cat-frozen-stare",1900,surpriseFreeze),
};

const Key sleepyNod[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.30f,0,2,.23f,.25f,0,0,.02f,3,0),
  K(.55f,0,3,.31f,.34f,0,0,.03f,5,0),
  K(.70f,0,0,-.38f,-.38f,0,0,0,-1,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key sleepyBlink[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.25f,0,0,.30f,.30f,0,0,0,1,0),
  K(.50f,0,0,.30f,.30f,0,0,0,1,0),
  K(.78f,0,0,-.30f,-.30f,0,0,0,-1,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key sleepyPeek[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.27f,-5,0,-.48f,.23f,0,0,-.025f,0,0),
  K(.70f,-5,0,-.48f,.23f,0,0,-.025f,0,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Clip sleepyClips[] = {
  CLIP("cat-drowsy-nod",2400,sleepyNod),
  CLIP("cat-long-blink",2300,sleepyBlink),
  CLIP("cat-sleepy-peek",2200,sleepyPeek),
};

const Key annoyedSquint[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.25f,0,0,.23f,.25f,0,.08f,0,0,0),
  K(.73f,0,0,.23f,.25f,0,.08f,0,0,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key annoyedSideEye[] = {
  K(0,0,0,0,0,0,0,0,0,0), KS(.17f,6,0,.08f,.22f,0,0,.02f,0,0,.48f),
  KS(.75f,6,0,.08f,.22f,0,0,.02f,0,0,.48f),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key annoyedDoubleBlink[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.20f,0,0,.48f,.48f,0,0,0,0,0),
  K(.33f,0,0,-.10f,-.10f,0,0,0,0,0),
  K(.48f,0,0,.48f,.48f,0,0,0,0,0),
  K(.64f,0,0,-.10f,-.10f,0,0,0,0,0), K(1,0,0,0,0,0,0,0,0,0)
};
const Clip annoyedClips[] = {
  CLIP("cat-unimpressed",1900,annoyedSquint),
  CLIP("cat-side-eye",1800,annoyedSideEye),
  CLIP("cat-impatient-blink",1800,annoyedDoubleBlink),
};

const Key dizzyWobble[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.13f,-9,-3,.18f,0,.03f,-.08f,-.05f,-3,0),
  K(.30f,10,2,0,.23f,0,0,.06f,3,0),
  K(.47f,-10,2,.23f,0,.03f,-.08f,-.06f,-2,0),
  K(.65f,9,-2,0,.22f,0,0,.05f,2,0),
  K(.84f,-5,0,.22f,.22f,0,0,-.03f,0,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Key dizzyCross[] = {
  K(0,0,0,0,0,0,0,0,0,0), K(.20f,0,0,.18f,.18f,.03f,-.09f,.04f,2,0),
  K(.42f,7,-2,.10f,.20f,0,-.10f,-.04f,-3,0),
  K(.66f,-7,1,.22f,.08f,.03f,-.10f,.04f,3,0),
  K(.83f,0,0,.46f,.46f,0,0,0,0,0),
  K(1,0,0,0,0,0,0,0,0,0)
};
const Clip dizzyClips[] = {
  CLIP("cat-dizzy-wobble",1600,dizzyWobble),
  CLIP("cat-dizzy-cross",1800,dizzyCross),
};

const Catalog catalogs[7] = {
  {neutralClips, static_cast<uint8_t>(sizeof(neutralClips)/sizeof(Clip))},
  {happyClips, static_cast<uint8_t>(sizeof(happyClips)/sizeof(Clip))},
  {sadClips, static_cast<uint8_t>(sizeof(sadClips)/sizeof(Clip))},
  {surpriseClips, static_cast<uint8_t>(sizeof(surpriseClips)/sizeof(Clip))},
  {sleepyClips, static_cast<uint8_t>(sizeof(sleepyClips)/sizeof(Clip))},
  {annoyedClips, static_cast<uint8_t>(sizeof(annoyedClips)/sizeof(Clip))},
  {dizzyClips, static_cast<uint8_t>(sizeof(dizzyClips)/sizeof(Clip))},
};
#undef K
#undef KS
#undef CLIP

float limit(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
float smooth(float t) {
  t = limit(t, 0, 1);
  return t * t * (3 - 2 * t);
}
// V02's open and focused outlines are sampled once. The render path then
// interpolates short scanline tables instead of solving Beziers per pixel.
constexpr int kShapeSamples = 104;
struct ControlPoint { float x, y; };
const ControlPoint focusLeft[13] = {
  {-25,-10},{-21,-19.33f},{-14,-23},{-4,-21},
  {9.33f,-17.67f},{19,-10.33f},{25,1},
  {19.67f,15.67f},{10.33f,23},{-3,23},
  {-17.67f,21},{-25,10},{-25,-10}
};
float openTop[kShapeSamples+1], openBottom[kShapeSamples+1];
float focusTop[kShapeSamples+1], focusBottom[kShapeSamples+1];
float roundPupilWidth[kShapeSamples+1], slitPupilWidth[kShapeSamples+1];
// Composed once per frame for each eye. The hot pixel loop only interpolates
// three ready-to-use spans, avoiding repeated shape/lid/slit blends.
float frameTop[2][kShapeSamples+1], frameBottom[2][kShapeSamples+1];
float framePupilWidth[2][kShapeSamples+1];
bool shapeTablesReady = false;
float cubic(float a, float b, float c, float d, float t) {
  const float q = 1-t;
  return q*q*q*a + 3*q*q*t*b + 3*q*t*t*c + t*t*t*d;
}
float focusYAtX(float x, bool upper) {
  const int first = upper ? (x <= focusLeft[3].x ? 0 : 3)
                          : (x >= focusLeft[9].x ? 6 : 9);
  const bool ascending = upper;
  float lo = 0, hi = 1;
  for (int i=0; i<12; ++i) {
    const float mid = (lo+hi)*.5f;
    const float bx = cubic(focusLeft[first].x,focusLeft[first+1].x,
                           focusLeft[first+2].x,focusLeft[first+3].x,mid);
    if (ascending ? bx < x : bx > x) lo = mid;
    else hi = mid;
  }
  const float t = (lo+hi)*.5f;
  return cubic(focusLeft[first].y,focusLeft[first+1].y,
               focusLeft[first+2].y,focusLeft[first+3].y,t);
}
void prepareShapeTables() {
  if (shapeTablesReady) return;
  for (int i=0; i<=kShapeSamples; ++i) {
    const float x = -25.5f+i*(51.0f/kShapeSamples);
    const float nx = x/25.5f;
    const float half = 26.5f*sqrtf(fmaxf(0,1-nx*nx));
    openTop[i] = -half;
    openBottom[i] = half;
    // The V02 focus path is 50 px wide versus the open path's 51 px.
    // Normalize widths here so morphing never leaves a detached tip pixel.
    const float fx = x*(25.0f/25.5f);
    focusTop[i] = focusYAtX(fx,true);
    focusBottom[i] = focusYAtX(fx,false);
    const float ny = (i-52)/52.0f;
    roundPupilWidth[i] = 19.5f*sqrtf(fmaxf(0,1-ny*ny));
    slitPupilWidth[i] = 6.375f*(1-powf(fabsf(ny),3.1f));
  }
  shapeTablesReady = true;
}
inline __attribute__((always_inline)) float sampleTable(const float* values, float index) {
  index = limit(index,0,kShapeSamples);
  const int i = static_cast<int>(index);
  if (i >= kShapeSamples) return values[kShapeSamples];
  const float f = index-i;
  return values[i]+(values[i+1]-values[i])*f;
}

// The six cubic segments below use the approved 240x280 tongue silhouette,
// translated to face-local coordinates. The short, wide shape deliberately
// has a steeper left edge, a fuller right shoulder, and an off-center tip.
// Each pose has identical Bezier topology so the contour itself can morph.
constexpr int kTonguePathPoints = 19;
constexpr int kTongueSegments = 6;
constexpr int kTongueStepsPerSegment = 6;
constexpr int kTongueScanRows = 53;
constexpr float kTongueScanTop = 47.0f;
constexpr float kTongueScanStep = .5f;
const ControlPoint tongueCompact[kTonguePathPoints] = {
  {-4,51}, {-2,50},{0,50},{2,51},
  {4,51},{5,52},{4,53}, {4,53},{3,54},{3,54},
  {1,55},{-1,55},{-2,54}, {-4,54},{-5,53},{-5,52},
  {-5,52},{-5,51},{-4,51}
};
const ControlPoint tongueMid[kTonguePathPoints] = {
  {-8,51}, {-5,49},{0,49},{3,50},
  {8,50},{11,52},{10,55}, {10,57},{8,58},{7,61},
  {5,65},{2,67},{-2,66}, {-7,65},{-11,63},{-12,59},
  {-13,56},{-11,53},{-8,51}
};
const ControlPoint tongueFull[kTonguePathPoints] = {
  {-11,52}, {-7,49},{-1,49},{3,50},
  {10,50},{14,53},{13,57}, {13,60},{10,61},{9,64},
  {7,68},{3,70},{-2,69}, {-8,68},{-12,65},{-14,60},
  {-15,57},{-14,54},{-11,52}
};
const ControlPoint tongueRight[kTonguePathPoints] = {
  {-11,52}, {-7,49},{-1,49},{4,50},
  {11,50},{15,53},{14,57}, {14,60},{12,62},{11,65},
  {10,69},{6,71},{1,70}, {-5,69},{-11,65},{-13,61},
  {-15,58},{-14,54},{-11,52}
};
const ControlPoint tongueNarrow[kTonguePathPoints] = {
  {-8,51}, {-5,49},{-1,49},{2,50},
  {7,49},{10,52},{9,55}, {9,57},{7,58},{6,60},
  {4,64},{1,66},{-2,65}, {-7,64},{-10,62},{-11,58},
  {-12,55},{-11,52},{-8,51}
};
struct TongueKey { float at; const ControlPoint* path; };
const TongueKey tongueKeys[] = {
  {0,tongueCompact}, {.12f,tongueCompact}, {.20f,tongueMid},
  {.28f,tongueFull}, {.38f,tongueRight}, {.47f,tongueFull},
  {.57f,tongueNarrow}, {.69f,tongueCompact}, {1,tongueCompact}
};
struct TongueScanlines {
  float left[kTongueScanRows], right[kTongueScanRows];
  float top, bottom;
};
constexpr int kTongueFrameCount = 73;  // one contour per 33 ms of a 2.4 s clip
TongueScanlines tongueFrames[kTongueFrameCount];
bool tongueFramesReady = false;
void tonguePathAt(float phase, ControlPoint (&path)[kTonguePathPoints]) {
  phase = limit(phase,0,1);
  uint8_t next = 1;
  while (next+1 < sizeof(tongueKeys)/sizeof(tongueKeys[0]) &&
         phase > tongueKeys[next].at) ++next;
  const TongueKey& a = tongueKeys[next-1];
  const TongueKey& b = tongueKeys[next];
  const float t = smooth((phase-a.at)/(b.at-a.at));
  for (int i=0; i<kTonguePathPoints; ++i) {
    path[i].x = a.path[i].x+(b.path[i].x-a.path[i].x)*t;
    path[i].y = a.path[i].y+(b.path[i].y-a.path[i].y)*t;
  }
}
void prepareTongueScanlines(const ControlPoint (&path)[kTonguePathPoints],
                            TongueScanlines& scan) {
  ControlPoint outline[kTongueSegments*kTongueStepsPerSegment+1];
  outline[0] = path[0];
  int count = 1;
  scan.top = 1000;
  scan.bottom = -1000;
  for (int segment=0; segment<kTongueSegments; ++segment) {
    const int base = segment*3;
    for (int step=1; step<=kTongueStepsPerSegment; ++step) {
      const float t = step/static_cast<float>(kTongueStepsPerSegment);
      ControlPoint& point = outline[count++];
      point.x = cubic(path[base].x,path[base+1].x,
                      path[base+2].x,path[base+3].x,t);
      point.y = cubic(path[base].y,path[base+1].y,
                      path[base+2].y,path[base+3].y,t);
    }
  }
  for (int j=0; j<count; ++j) {
    scan.top = fminf(scan.top,outline[j].y);
    scan.bottom = fmaxf(scan.bottom,outline[j].y);
  }
  for (int i=0; i<kTongueScanRows; ++i) {
    const float y = kTongueScanTop+i*kTongueScanStep;
    float left = 1000, right = -1000;
    for (int j=0; j<count-1; ++j) {
      const ControlPoint& a = outline[j];
      const ControlPoint& b = outline[j+1];
      if ((a.y <= y && b.y > y) || (b.y <= y && a.y > y)) {
        const float x = a.x+(y-a.y)*(b.x-a.x)/(b.y-a.y);
        left = fminf(left,x);
        right = fmaxf(right,x);
      }
    }
    scan.left[i] = left;
    scan.right[i] = right;
  }
}
uint8_t tongueCoverage(const TongueScanlines& scan, float x, float y) {
  const float index = (y-kTongueScanTop)/kTongueScanStep;
  if (index < 0 || index >= kTongueScanRows-1 ||
      y < scan.top-.5f || y > scan.bottom+.5f) return 0;
  const int i = static_cast<int>(index);
  const bool aValid = scan.left[i] < scan.right[i];
  const bool bValid = scan.left[i+1] < scan.right[i+1];
  if (!aValid && !bValid) return 0;
  const float t = index-i;
  const float left = aValid && bValid ?
    scan.left[i]+(scan.left[i+1]-scan.left[i])*t :
    (aValid ? scan.left[i] : scan.left[i+1]);
  const float right = aValid && bValid ?
    scan.right[i]+(scan.right[i+1]-scan.right[i])*t :
    (aValid ? scan.right[i] : scan.right[i+1]);
  const float edge = fminf(fminf(x-left,right-x),
                           fminf(y-scan.top,scan.bottom-y));
  return static_cast<uint8_t>(255*limit(.5f+edge,0,1)+.5f);
}
void prepareTongueFrames() {
  if (tongueFramesReady) return;
  ControlPoint path[kTonguePathPoints];
  for (int i=0; i<kTongueFrameCount; ++i) {
    tonguePathAt(i/static_cast<float>(kTongueFrameCount-1),path);
    prepareTongueScanlines(path,tongueFrames[i]);
  }
  tongueFramesReady = true;
}
// Build a single two-turn gold line. It is sampled while the eye moves;
// no trigonometry or allocation is needed for individual eye pixels.
void prepareSpiralMask() {
  if (spiralMaskReady) return;
  constexpr float turn = 6.2831853f;
  constexpr float pitch = 7.0f / turn;
  for (int y = 0; y < kSpiralSize; ++y) {
    const float dy = (y - kSpiralCenter) * kSpiralPixelsPerTexel;
    for (int x = 0; x < kSpiralSize; ++x) {
      const float dx = (x - kSpiralCenter) * kSpiralPixelsPerTexel;
      const float radius = sqrtf(dx * dx + dy * dy);
      float angle = atan2f(dy, dx);
      if (angle < 0) angle += turn;
      const float firstTurn = 2.2f + pitch * angle;
      const float distance = fminf(fabsf(radius - firstTurn),
                                    fabsf(radius - firstTurn - 7.0f));
      float alpha = limit(1.95f - distance, 0, 1);
      // The small central curl makes the shape read as a spiral, not rings.
      alpha = fmaxf(alpha, limit(2.2f - radius, 0, 1));
      spiralMask[y * kSpiralSize + x] = static_cast<uint8_t>(alpha * 255 + .5f);
    }
  }
  spiralMaskReady = true;
}
uint8_t sampleSpiral(float x, float y) {
  constexpr float texelsPerPixel = 1.0f / kSpiralPixelsPerTexel;
  const float fx = x * texelsPerPixel + kSpiralCenter;
  const float fy = y * texelsPerPixel + kSpiralCenter;
  if (fx < 0 || fy < 0 || fx >= kSpiralSize - 1 || fy >= kSpiralSize - 1) return 0;
  const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
  const float tx = fx - ix, ty = fy - iy;
  const int at = iy * kSpiralSize + ix;
  const float upper = spiralMask[at] +
    (spiralMask[at + 1] - spiralMask[at]) * tx;
  const float lower = spiralMask[at + kSpiralSize] +
    (spiralMask[at + kSpiralSize + 1] - spiralMask[at + kSpiralSize]) * tx;
  return static_cast<uint8_t>(upper + (lower - upper) * ty + .5f);
}
const Clip& activeClip(PetMood mood, uint8_t index) {
  const uint8_t m = static_cast<uint8_t>(mood);
  const Catalog& catalog = catalogs[m < 7 ? m : 0];
  return catalog.clips[index < catalog.count ? index : 0];
}
float interp(const Clip& clip, uint8_t channel, uint32_t elapsedMs) {
  const float p = limit(elapsedMs / static_cast<float>(clip.durationMs), 0, 1);
  uint8_t i = 1;
  while (i + 1 < clip.count && p > clip.keys[i].at) ++i;
  const Key& a = clip.keys[i-1];
  const Key& b = clip.keys[i];
  const float t = smooth((p-a.at)/(b.at-a.at));
  return a.value[channel] + (b.value[channel]-a.value[channel])*t;
}
uint8_t coverage(float x, float y, float invRx2, float invRy2, float edgeScale) {
  const float q = x*x*invRx2 + y*y*invRy2;
  const float a = limit(.5f + (1-q)*edgeScale, 0, 1);
  return static_cast<uint8_t>(a*255 + .5f);
}
uint16_t blend565(uint16_t base, uint16_t over, uint8_t alpha) {
  if (alpha == 0) return base;
  if (alpha == 255) return over;
  const uint16_t ia = 255 - alpha;
  const uint16_t r = (((base >> 11) * ia + (over >> 11) * alpha + 127) / 255) & 31;
  const uint16_t g = (((base >> 5 & 63) * ia + (over >> 5 & 63) * alpha + 127) / 255) & 63;
  const uint16_t b = (((base & 31) * ia + (over & 31) * alpha + 127) / 255) & 31;
  return (r << 11) | (g << 5) | b;
}
struct EyeGeometry {
  float x, shapeInvScale;
  const float* top;
  const float* bottom;
  const float* pupilWidth;
  float pupilX, pupilY, pupilMaxWidth, pupilHeight, pupilInvHeight;
  float shineX, shineY, shineRx, shineRy, shineInvX2, shineInvY2, shineEdge;
  float pupilVisible, shineVisible;
  float spiralScaleX, spiralScaleY, spiralCos, spiralSin;
  bool dizzy;
};
EyeGeometry geometry(float centerX, float lid, float gazeX, float gazeY,
                     float eyeScale, float pupilScale, float slit,
                     float gazeSpeed, float gazeDirection, bool dizzy,
                     float spiralAngle) {
  const float size = limit(1+eyeScale, .72f, 1.30f);
  lid = limit(lid,0,1);
  slit = dizzy ? 0 : limit(slit,0,1);
  const float pupilSize = limit(1+pupilScale, .58f, 1.18f);
  const float pupilHeight = (20.5f+2.5f*slit)*pupilSize*(1-.08f*lid);
  const float side = centerX < 0 ? 1.3f : -1.3f;
  const bool right = centerX > 0;
  const uint8_t eyeIndex = right ? 1 : 0;
  const float upperClose = 1-powf(1-lid,1.55f);
  const float lowerClose = 1-powf(1-lid,.65f);
  const float closure = 2.2f*size;
  for (int i=0; i<=kShapeSamples; ++i) {
    const int pathIndex = right ? kShapeSamples-i : i;
    const float upper = (openTop[i]+(focusTop[pathIndex]-openTop[i])*slit)*size;
    const float lower = (openBottom[i]+(focusBottom[pathIndex]-openBottom[i])*slit)*size;
    frameTop[eyeIndex][i] = upper+(closure-upper)*upperClose;
    frameBottom[eyeIndex][i] = lower+(closure-lower)*lowerClose;
    framePupilWidth[eyeIndex][i] =
      (roundPupilWidth[i]+(slitPupilWidth[i]-roundPupilWidth[i])*slit)*pupilSize;
  }
  EyeGeometry g = {};
  g.x = centerX;
  g.shapeInvScale = 1/(size*(1-.039f*smooth(lid)));
  g.top = frameTop[eyeIndex];
  g.bottom = frameBottom[eyeIndex];
  g.pupilWidth = framePupilWidth[eyeIndex];
  g.pupilX = centerX + side*(1-slit) + gazeX;
  g.pupilY = -2.2f+gazeY-1.15f*lid;
  g.pupilMaxWidth = (19.5f+(6.375f-19.5f)*slit)*pupilSize;
  g.pupilHeight = pupilHeight;
  g.pupilInvHeight = 1/pupilHeight;
  const float dilation = limit(pupilScale/.16f,0,1);
  const float shineRadius = (3.7f-.35f*slit+.4f*dilation)*fminf(1,size);
  const float shineRx = shineRadius*(1+.12f*gazeSpeed);
  const float shineRy = shineRadius*(1-.09f*gazeSpeed);
  g.shineX = centerX-7+3.8f*slit+.28f*gazeX+.38f*gazeSpeed*gazeDirection;
  g.shineY = -10+.25f*gazeY-.16f*gazeSpeed;
  g.shineRx = shineRx;
  g.shineRy = shineRy;
  g.shineInvX2 = 1/(shineRx*shineRx);
  g.shineInvY2 = 1/(shineRy*shineRy);
  g.shineEdge = .5f*fminf(shineRx,shineRy);
  g.pupilVisible = 1-smooth((lid-.75f)/.23f);
  g.shineVisible = dizzy ? 0 : 1-smooth((lid-.58f)/.30f);
  g.dizzy = dizzy;
  if (dizzy) {
    g.spiralScaleX = 1/pupilSize;
    g.spiralScaleY = 19.5f/pupilHeight;
    g.spiralCos = cosf(spiralAngle);
    g.spiralSin = sinf(spiralAngle);
  }
  return g;
}
void shadeEye(uint16_t* pixel, float u, float v, const EyeGeometry& eye) {
  // The upper lid falls first; the lower lid rises more gently. Iris, pupil
  // and highlight remain full-sized shapes clipped by this changing contour.
  const float outlineX = (u-eye.x)*eye.shapeInvScale;
  if (outlineX < -25.5f || outlineX > 25.5f) return;
  const float index = (outlineX+25.5f)*(kShapeSamples/51.0f);
  const float top = sampleTable(eye.top,index);
  const float bottom = sampleTable(eye.bottom,index);
  if (bottom <= top) return;
  const float sideEdge = (25.5f-fabsf(outlineX))/eye.shapeInvScale;
  const float edge = fminf(fminf(.5f+v-top,.5f+bottom-v),.5f+sideEdge);
  const uint8_t gold = static_cast<uint8_t>(255*limit(edge,0,1)*
    limit((bottom-top)*.5f,0,1)+.5f);
  if (!gold) return;
  uint16_t color = blend565(0,kGold,gold);
  if (eye.pupilVisible > 0 &&
      fabsf(u-eye.pupilX) < eye.pupilMaxWidth+.5f &&
      fabsf(v-eye.pupilY) < eye.pupilHeight+.5f) {
    const float dy = v-eye.pupilY;
    uint8_t black = 0;
    const float normalizedY = dy*eye.pupilInvHeight;
    if (fabsf(normalizedY) < 1.02f) {
      const float pupilIndex = (normalizedY+1)*(kShapeSamples*.5f);
      const float width = sampleTable(eye.pupilWidth,pupilIndex);
      const float pupilEdge = fminf(.5f+width-fabsf(u-eye.pupilX),
                                    .5f+eye.pupilHeight-fabsf(dy));
      black = static_cast<uint8_t>(255*limit(pupilEdge,0,1)*eye.pupilVisible+.5f);
    }
    color = blend565(color,0,black);
    if (eye.dizzy && black) {
      const float dx = (u-eye.pupilX)*eye.spiralScaleX;
      const float dy = (v-eye.pupilY)*eye.spiralScaleY;
      const uint8_t line = sampleSpiral(eye.spiralCos*dx-eye.spiralSin*dy,
                                        eye.spiralSin*dx+eye.spiralCos*dy);
      uint8_t alpha = static_cast<uint8_t>((static_cast<uint16_t>(line)*black+127)/255);
      if (alpha > gold) alpha = gold;
      color = blend565(color,kGold,alpha);
    }
  }
  if (eye.shineVisible > 0 &&
      fabsf(u-eye.shineX) < eye.shineRx+.5f &&
      fabsf(v-eye.shineY) < eye.shineRy+.5f) {
    uint8_t shine = coverage(u-eye.shineX,v-eye.shineY,
                             eye.shineInvX2,eye.shineInvY2,eye.shineEdge);
    if (shine > gold) shine = gold;
    color = blend565(color,kWhite,static_cast<uint8_t>(shine*eye.shineVisible));
  }
  *pixel = color;
}
} // namespace

BlackCatPet::Pose BlackCatPet::base(PetMood mood) {
  switch (mood) {
    case PetMood::Happy: return {0,0,.04f,.04f,.03f,.03f,0,0,0,0};
    case PetMood::Sad: return {0,2,.34f,.37f,-.04f,0,0,-.025f,2,0};
    case PetMood::Surprised: return {0,-1,0,0,.10f,.10f,0,0,-1,0};
    case PetMood::Sleepy: return {0,2,.60f,.63f,-.02f,0,0,0,2,0};
    case PetMood::Annoyed: return {0,0,.52f,.55f,0,-.03f,0,0,0,0};
    case PetMood::Dizzy: return {0,0,.08f,.08f,.04f,-.08f,0,0,0,0};
    default: return {0,0,0,0,0,0,0,0,0,0};
  }
}

uint32_t BlackCatPet::randomWord() {
  _rng ^= _rng << 13;
  _rng ^= _rng >> 17;
  _rng ^= _rng << 5;
  return _rng;
}

void BlackCatPet::begin(Arduino_Canvas* canvas, uint32_t now) {
  prepareSpiralMask();
  prepareShapeTables();
  prepareTongueFrames();
  _canvas = canvas;
  _mood = PetMood::Neutral;
  _pose = _from = base(_mood);
  _wasSleeping = false;
  _rng ^= micros();
  if (!_rng) _rng = 1;
  for (uint8_t& index : _lastClip) index = 255;
  _starts = 0;
  _transitionAt = now - 300;
  _lastRenderAt = 0;
  _motion.begin(now, randomWord());
  _motion.setBounds(26, 47);
  _touch.cancel();
  _dragX = _dragY = _dragVx = _dragVy = 0;
  _tapPulseValid = false;
  _strokes = _drags = 0;
  startClip(now);
}

void BlackCatPet::touchDown(uint16_t x, uint16_t y, uint32_t now) {
  _touch.down(x,y,_x,_y,now);
}

CatTouchModel::Mode BlackCatPet::touchMove(uint16_t x, uint16_t y) {
  if (!_touch.move(x,y)) return CatTouchModel::Mode::None;
  const auto mode = _touch.mode();
  if (mode == CatTouchModel::Mode::Stroke) ++_strokes;
  if (mode == CatTouchModel::Mode::Drag) ++_drags;
  return mode;
}

void BlackCatPet::touchUp(uint32_t now) { _touch.release(now); }

void BlackCatPet::cancelTouch() {
  _touch.cancel();
  _dragX = _dragY = _dragVx = _dragVy = 0;
  _tapPulseValid = false;
}

void BlackCatPet::tapPulse(uint16_t x, uint16_t y, uint32_t now) {
  _tapX = x;
  _tapY = y;
  _tapPulseAt = now;
  _tapPulseValid = true;
}

const char* BlackCatPet::touchModeName() const {
  switch (_touch.mode()) {
    case CatTouchModel::Mode::Look: return "look";
    case CatTouchModel::Mode::Stroke: return "stroke";
    case CatTouchModel::Mode::Drag: return "drag";
    default: return "none";
  }
}

void BlackCatPet::startClip(uint32_t now) {
  const uint8_t m = static_cast<uint8_t>(_mood);
  const Catalog& catalog = catalogs[m < 7 ? m : 0];
  uint8_t next = static_cast<uint8_t>(randomWord() % catalog.count);
  if (catalog.count > 1 && next == _lastClip[m])
    next = static_cast<uint8_t>((next + 1 + randomWord() % (catalog.count-1)) % catalog.count);
  _clipIndex = next;
  _lastClip[m] = next;
  _clipAt = now;
  ++_starts;
}

void BlackCatPet::setMood(PetMood mood, uint32_t now) {
  if (static_cast<uint8_t>(mood) >= 7) mood = PetMood::Neutral;
  _from = _pose;
  _mood = mood;
  _transitionAt = now;
  startClip(now);
}

const char* BlackCatPet::animationName() const {
  if (_wasSleeping) return "cat-sleeping";
  return activeClip(_mood,_clipIndex).name;
}

BlackCatPet::Pose BlackCatPet::compose(uint32_t now, bool sleeping) {
  const Clip& clip = activeClip(_mood,_clipIndex);
  Pose p = base(_mood);
  const uint32_t elapsed = now-_clipAt;
  p.gazeX += interp(clip,GazeX,elapsed);
  p.gazeY += interp(clip,GazeY,elapsed);
  p.leftLid += interp(clip,LeftLid,elapsed);
  p.rightLid += interp(clip,RightLid,elapsed);
  p.eyeScale += interp(clip,EyeScale,elapsed);
  p.pupilScale += interp(clip,PupilScale,elapsed);
  p.pupilSlit += interp(clip,PupilSlit,elapsed);
  p.tilt += interp(clip,Tilt,elapsed);
  p.bob += interp(clip,Bob,elapsed);
  p.tongue += interp(clip,Tongue,elapsed);
  if (_mood == PetMood::Dizzy && !sleeping) {
    p.gazeX += 3*sinf(now/95.0f);
    p.tilt += .04f*sinf(now/155.0f);
  }
  if (sleeping) {
    p.gazeX = p.gazeY = 0;
    p.leftLid = p.rightLid = 1.0f;
    p.pupilSlit = 0;
    p.tongue = 0;
    p.tilt = 0;
    p.bob = 2 + .7f*sinf(now/900.0f);
  }
  const float transition = smooth((now-_transitionAt)/260.0f);
  if (transition < 1) {
    p.gazeX = _from.gazeX + (p.gazeX-_from.gazeX)*transition;
    p.gazeY = _from.gazeY + (p.gazeY-_from.gazeY)*transition;
    p.leftLid = _from.leftLid + (p.leftLid-_from.leftLid)*transition;
    p.rightLid = _from.rightLid + (p.rightLid-_from.rightLid)*transition;
    p.eyeScale = _from.eyeScale + (p.eyeScale-_from.eyeScale)*transition;
    p.pupilScale = _from.pupilScale + (p.pupilScale-_from.pupilScale)*transition;
    p.pupilSlit = _from.pupilSlit + (p.pupilSlit-_from.pupilSlit)*transition;
    p.tilt = _from.tilt + (p.tilt-_from.tilt)*transition;
    p.bob = _from.bob + (p.bob-_from.bob)*transition;
    p.tongue = _from.tongue + (p.tongue-_from.tongue)*transition;
  }
  // Contact feedback is layered over the catalog, never restarting a random
  // animation just because the finger moved. Looking begins on DOWN; a true
  // tap is confirmed by TouchGesture on UP and receives a short eye pulse.
  if (!sleeping) {
    const bool touching = _touch.active();
    const float touchWeight = touching ?
      smooth((now-_touch.downAt())/65.0f) :
      (_touch.releasedMode() != CatTouchModel::Mode::None ?
        CatTouchDecay::factor(now,_touch.releaseAt(),260.0f) : 0);
    const float tapWeight = _tapPulseValid && now-_tapPulseAt < 500 ?
      CatTouchDecay::factor(now,_tapPulseAt,180.0f) : 0;
    const float attention = fmaxf(touchWeight,tapWeight);
    if (attention > .001f) {
      const float targetX = touching ? _touch.x() :
        (tapWeight > touchWeight ? _tapX : _touch.x());
      const float targetY = touching ? _touch.y() :
        (tapWeight > touchWeight ? _tapY : _touch.y());
      const float gazeX = limit((targetX-_x)*.16f,-12,12);
      const float gazeY = limit((targetY-_y)*.11f,-6,6);
      p.gazeX += (gazeX-p.gazeX)*attention;
      p.gazeY += (gazeY-p.gazeY)*attention;
      const float downPulse = touching ?
        CatTouchDecay::factor(now,_touch.downAt(),130.0f) : 0;
      p.eyeScale += .035f*downPulse+.055f*tapWeight;
      p.leftLid += .055f*downPulse;
      p.rightLid += .055f*downPulse;
      if (_mood == PetMood::Dizzy) {
        // A touch steadies the wobble briefly, but does not erase spiral eyes.
        p.tilt *= 1-.42f*attention;
        p.leftLid += .10f*attention;
        p.rightLid += .10f*attention;
      }
    }
    float stroke = 0;
    if (_touch.mode() == CatTouchModel::Mode::Stroke)
      stroke = .18f+.48f*_touch.strokeStrength();
    else if (_touch.releasedMode() == CatTouchModel::Mode::Stroke &&
             now-_touch.releaseAt() < 1000)
      stroke = (.18f+.48f*_touch.strokeStrength())*
               CatTouchDecay::factor(now,_touch.releaseAt(),410.0f);
    p.leftLid += stroke;
    p.rightLid += stroke;
    p.eyeScale -= .055f*stroke;
  }
  p.gazeX = limit(p.gazeX,-15,15);
  p.gazeY = limit(p.gazeY,-7,7);
  p.leftLid = limit(p.leftLid,0,1);
  p.rightLid = limit(p.rightLid,0,1);
  p.pupilSlit = limit(p.pupilSlit,0,1);
  p.tongue = limit(p.tongue,0,1);
  return p;
}

void BlackCatPet::thumbnail(int x, int y) {
  if (!_canvas) return;
  // Same flat palette, reduced geometry. The menu already clears its canvas.
  _canvas->fillCircle(x+18,y+38,9,kGold);
  _canvas->fillCircle(x+46,y+38,9,kGold);
  _canvas->fillCircle(x+19,y+37,6,0);
  _canvas->fillCircle(x+45,y+37,6,0);
  _canvas->fillCircle(x+15,y+34,2,kWhite);
  _canvas->fillCircle(x+43,y+34,2,kWhite);
}

void BlackCatPet::render(uint32_t now, bool sleeping) {
  if (!_canvas) return;
  if (sleeping != _wasSleeping) {
    _from = _pose;
    _transitionAt = now;
    _wasSleeping = sleeping;
  }
  if (!sleeping && now-_clipAt >= activeClip(_mood,_clipIndex).durationMs)
    startClip(now);
  const Pose nextPose = compose(now,sleeping);
  const float gazeDelta = nextPose.gazeX-_pose.gazeX;
  const float frameMs = _lastRenderAt ? limit(now-_lastRenderAt,1,100) : 33;
  const float gazeSpeed = _lastRenderAt ?
    limit(fabsf(gazeDelta)*1000/(frameMs*30),0,1) : 0;
  const float gazeDirection = gazeDelta > 0 ? 1.0f : (gazeDelta < 0 ? -1.0f : 0);
  _pose = nextPose;
  _lastRenderAt = now;

  const float angle = _motion.tilt()+_pose.tilt;
  const float c = cosf(angle), s = sinf(angle);
  const float sx = _motion.impactScaleX(), sy = _motion.impactScaleY();
  // Keep the entire eye pair and optional tongue inside the rounded LCD.
  const float eyeHalfX = kEyeSeparation+25.5f*limit(1+_pose.eyeScale,.72f,1.30f)+2;
  const float eyeHalfY = 26.5f*limit(1+_pose.eyeScale,.72f,1.30f)+2;
  const float lower = _pose.tongue > .005f ? 73.0f : eyeHalfY;
  const float xExtent = fabsf(c*sx)*eyeHalfX+fabsf(s*sy)*fmaxf(eyeHalfY,lower);
  const float yExtent = fabsf(s*sx)*eyeHalfX+fabsf(c*sy)*fmaxf(eyeHalfY,lower);
  _motion.setBounds(fmaxf(0,120-xExtent-3),fmaxf(0,140-yExtent-6));
  _motion.update(now,sleeping,_mood==PetMood::Dizzy);

  // The motion update can change lean/impact slightly: refresh the affine.
  const float theta = _motion.tilt()+_pose.tilt;
  const float co = cosf(theta), si = sinf(theta);
  const float scaleX = _motion.impactScaleX();
  const float scaleY = _motion.impactScaleY();
  const float dt = fminf(frameMs,50.0f)*.001f;
  if (_touch.mode() == CatTouchModel::Mode::Drag) {
    const float targetX = limit(_touch.dragX(),-72,72);
    const float targetY = limit(_touch.dragY(),-62,62);
    const float blend = 1-expf(-frameMs/24.0f);
    const float oldX = _dragX, oldY = _dragY;
    _dragX += (targetX-_dragX)*blend;
    _dragY += (targetY-_dragY)*blend;
    _dragVx = limit((_dragX-oldX)/dt,-650,650);
    _dragVy = limit((_dragY-oldY)/dt,-650,650);
  } else {
    // Slightly underdamped return gives a tactile bounce without a hard snap.
    _dragVx += (-160*_dragX-13*_dragVx)*dt;
    _dragVy += (-160*_dragY-13*_dragVy)*dt;
    _dragX += _dragVx*dt;
    _dragY += _dragVy*dt;
    if (fabsf(_dragX) < .1f && fabsf(_dragVx) < .8f) _dragX = _dragVx = 0;
    if (fabsf(_dragY) < .1f && fabsf(_dragVy) < .8f) _dragY = _dragVy = 0;
  }
  _x = limit(120+_motion.x()+_dragX,xExtent+3,240-xExtent-3);
  _y = limit(140+_motion.y()+_pose.bob+_dragY,yExtent+6,280-yExtent-6);

  const auto transformedRect = [&](float minLocalX,float maxLocalX,
                                   float minLocalY,float maxLocalY) -> Rect {
    float minX=1000,maxX=-1000,minY=1000,maxY=-1000;
    for (uint8_t i=0; i<4; ++i) {
      const float lx = (i&1) ? maxLocalX : minLocalX;
      const float ly = (i&2) ? maxLocalY : minLocalY;
      const float px = _x+co*scaleX*lx-si*scaleY*ly;
      const float py = _y+si*scaleX*lx+co*scaleY*ly;
      minX=fminf(minX,px); maxX=fmaxf(maxX,px);
      minY=fminf(minY,py); maxY=fmaxf(maxY,py);
    }
    return {
      static_cast<int16_t>(limit(floorf(minX-1),0,239)),
      static_cast<int16_t>(limit(floorf(minY-1),0,279)),
      static_cast<int16_t>(limit(ceilf(maxX+1),0,239)),
      static_cast<int16_t>(limit(ceilf(maxY+1),0,279))
    };
  };
  _eyeRect = transformedRect(-eyeHalfX,eyeHalfX,-eyeHalfY,eyeHalfY);
  _tongueRect = _pose.tongue > .005f ?
    transformedRect(-16,16,46,73) : Rect{0,0,-1,-1};
  const int left = _tongueRect.right >= _tongueRect.left ?
    min(_eyeRect.left,_tongueRect.left) : _eyeRect.left;
  const int right = _tongueRect.right >= _tongueRect.left ?
    max(_eyeRect.right,_tongueRect.right) : _eyeRect.right;
  _top = _tongueRect.right >= _tongueRect.left ?
    min(_eyeRect.top,_tongueRect.top) : _eyeRect.top;
  _bottom = _tongueRect.right >= _tongueRect.left ?
    max(_eyeRect.bottom,_tongueRect.bottom) : _eyeRect.bottom;

  _canvas->fillScreen(0);
  uint16_t* fb = _canvas->getFramebuffer();
  const bool dizzyEyes = _mood == PetMood::Dizzy && !sleeping;
  const float dizzySeconds = (now-_transitionAt)*.001f;
  const float slowingSeconds = fminf(dizzySeconds,6.0f);
  const float spiralPhase = 8.0f*slowingSeconds-.5f*slowingSeconds*slowingSeconds+
                            2.0f*fmaxf(0,dizzySeconds-6.0f);
  const EyeGeometry leftEye = geometry(-kEyeSeparation,_pose.leftLid,
    _pose.gazeX,_pose.gazeY,_pose.eyeScale,_pose.pupilScale,_pose.pupilSlit,
    gazeSpeed,gazeDirection,dizzyEyes,spiralPhase);
  const EyeGeometry rightEye = geometry(kEyeSeparation,_pose.rightLid,
    _pose.gazeX,_pose.gazeY,_pose.eyeScale,_pose.pupilScale,_pose.pupilSlit,
    gazeSpeed,gazeDirection,dizzyEyes,-spiralPhase+.8f);
  const float ux = co/scaleX, uy = si/scaleX;
  const float vx = -si/scaleY, vy = co/scaleY;
  const float tongueAlpha = _pose.tongue;
  const TongueScanlines* tongueScan = nullptr;
  if (tongueAlpha > .005f) {
    float tonguePhase = .28f;  // full contour while an old clip fades out
    if (!sleeping && _mood == PetMood::Happy && _clipIndex < 2) {
      const Clip& clip = activeClip(_mood,_clipIndex);
      tonguePhase = limit((now-_clipAt)/static_cast<float>(clip.durationMs),0,1);
    }
    const int frame = static_cast<int>(tonguePhase*(kTongueFrameCount-1)+.5f);
    tongueScan = &tongueFrames[frame];
  }
  for (int y=_top; y<=_bottom; ++y) {
    const float dx = left+.5f-_x, dy = y+.5f-_y;
    float u = ux*dx+uy*dy, v = vx*dx+vy*dy;
    uint16_t* row = fb+y*240;
    for (int x=left; x<=right; ++x,u+=ux,v+=vx) {
      if (v >= -eyeHalfY && v <= eyeHalfY) {
        if (u > -eyeHalfX && u < 0)
          shadeEye(row+x,u,v,leftEye);
        else if (u > 0 && u < eyeHalfX)
          shadeEye(row+x,u,v,rightEye);
      }
      if (tongueAlpha > .005f && u > -16 && u < 16 && v > 46 && v < 73) {
        const uint8_t a = tongueCoverage(*tongueScan,u,v);
        if (a) row[x] = blend565(row[x],kPink,static_cast<uint8_t>(a*tongueAlpha));
      }
    }
  }
}
