// Compile the production black-cat renderer with a framebuffer-only host
// canvas. No board, serial port or flash is accessed by this test.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#define private public
#include "../../sketch/DeskPetV01/BlackCatPet.h"
#undef private
#include "../../sketch/DeskPetV01/CatTouchDecay.h"

void require(bool condition,const char* why) {
  if (!condition) throw std::runtime_error(why);
}

size_t lit(const Arduino_Canvas& canvas) {
  return std::count_if(canvas.pixels.begin(),canvas.pixels.end(),
                       [](uint16_t pixel) { return pixel!=0; });
}

void finitePose(const BlackCatPet& cat) {
  const auto& p=cat._pose;
  const float values[] = {p.gazeX,p.gazeY,p.leftLid,p.rightLid,p.eyeScale,
    p.pupilScale,p.pupilSlit,p.tilt,p.bob,p.tongue,cat._dragX,cat._dragY,
    cat._dragVx,cat._dragVy,cat.positionX(),cat.positionY()};
  for (float value:values) require(std::isfinite(value),"non-finite cat pose or drag position");
  require(cat.drawTop()>=0 && cat.drawBottom()<280 &&
          cat.drawTop()<=cat.drawBottom(),"invalid cat draw bounds");
}

int main() {
  // This is the original expression's actual unsigned-negation behavior.
  const uint32_t elapsed=33;
  const float legacy=std::exp(-(elapsed)/130.0f);
  require(std::isinf(legacy),"old unsigned-negation failure did not reproduce");
  std::cout<<"PASS old unsigned negative elapsed -> positive infinity\n";

  float previous=1.0f;
  for (uint32_t ms=0;ms<=5000;ms+=10) {
    const float value=CatTouchDecay::factor(5000+ms,5000,130.0f);
    require(std::isfinite(value) && value>=0 && value<=previous,
            "decay must stay finite and monotonic");
    previous=value;
  }
  require(CatTouchDecay::factor(5000,5000,130.0f)==1.0f,
          "decay at contact start must be one");
  require(CatTouchDecay::factor(9000,5000,130.0f)<0.0001f,
          "long contact decay did not approach zero");
  const float wrapped=CatTouchDecay::factor(0x10u,0xfffffff0u,130.0f);
  require(std::isfinite(wrapped) &&
          std::fabs(wrapped-std::exp(-32.0f/130.0f))<0.00001f,
          "millis wraparound changed the decay interval");
  std::cout<<"PASS finite monotonic contact decay and millis wraparound\n";

  Arduino_Canvas canvas;
  BlackCatPet cat;
  const uint32_t start=5000;
  cat.begin(&canvas,start);
  cat.setMood(PetMood::Neutral,start);
  cat.render(start,false);
  require(lit(canvas)>100,"idle face did not draw");

  cat.touchDown(120,140,start+20);
  unsigned visibleDragFrames=0;
  for (unsigned frame=1;frame<=105;++frame) {
    const uint32_t now=start+20+frame*33;
    cat.touchMove(static_cast<uint16_t>(120+std::min(frame*2u,80u)),
                  static_cast<uint16_t>(140+std::min(frame,35u)));
    cat.render(now,false);
    finitePose(cat);
    if (lit(canvas)>100) ++visibleDragFrames;
  }
  require(cat._touch.mode()==CatTouchModel::Mode::Drag,"drag was not recognized");
  require(visibleDragFrames>=95,"eyes vanished during sustained drag");
  std::cout<<"PASS production renderer stays visible and finite during 3.5-second drag\n";

  cat.touchUp(start+20+105*33);
  unsigned visibleReleaseFrames=0;
  for (unsigned frame=1;frame<=70;++frame) {
    cat.render(start+20+(105+frame)*33,false);
    finitePose(cat);
    if (lit(canvas)>100) ++visibleReleaseFrames;
  }
  require(visibleReleaseFrames>=62,"eyes vanished after drag release");
  std::cout<<"PASS production renderer returns visibly after release\n";

  const uint32_t tapAt=start+20+175*33;
  cat.tapPulse(130,140,tapAt);
  cat.render(tapAt+33,false);
  finitePose(cat);
  require(lit(canvas)>100,"tap pulse blanked the eyes");
  std::cout<<"PASS tap pulse stays visible and finite\n";

  const uint32_t strokeAt=tapAt+100;
  cat.touchDown(120,static_cast<uint16_t>(cat.positionY()-30),strokeAt);
  cat.touchMove(140,static_cast<uint16_t>(cat.positionY()-30));
  require(cat._touch.mode()==CatTouchModel::Mode::Stroke,"brow stroke was not recognized");
  cat.touchUp(strokeAt+20);
  cat.render(strokeAt+53,false);
  finitePose(cat);
  require(lit(canvas)>100,"released stroke blanked the eyes");
  std::cout<<"PASS released brow stroke stays visible and finite\n";
}
