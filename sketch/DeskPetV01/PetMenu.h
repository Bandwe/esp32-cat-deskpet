#pragma once
#include <Arduino_GFX_Library.h>
#include "SagePet.h"
#include "BlackCatPet.h"
#include "MenuLabels.h"

namespace PetMenu {
// The visible corner control is reserved from the top-edge swipe zone. Its
// whole contact is consumed by the caller so pressing it cannot pet or select.
inline bool buttonHit(int x, int y) {
  return x>=202 && x<=239 && y>=0 && y<=38;
}
inline void drawButton(Arduino_Canvas& canvas, uint8_t selectedSkin) {
  const uint16_t ink = selectedSkin == 2 ? 0xfecf : 0xffff;
  canvas.fillRoundRect(205,3,31,32,8,0);
  canvas.drawRoundRect(205,3,31,32,8,0x4208);
  canvas.drawCircle(220,19,7,ink);
  canvas.drawCircle(220,19,3,ink);
  canvas.drawLine(220,9,220,12,ink);
  canvas.drawLine(220,26,220,29,ink);
  canvas.drawLine(210,19,213,19,ink);
  canvas.drawLine(227,19,230,19,ink);
  canvas.drawLine(213,12,215,14,ink);
  canvas.drawLine(225,24,227,26,ink);
  canvas.drawLine(213,26,215,24,ink);
  canvas.drawLine(225,14,227,12,ink);
}
inline void label(Arduino_Canvas& canvas, const MenuLabels::Label& text,
                  int centerX, int top, uint8_t brightness=255) {
  uint16_t* fb = canvas.getFramebuffer();
  const int left = centerX-text.width/2;
  for (unsigned y=0; y<text.height; ++y) for (unsigned x=0; x<text.width; ++x) {
    if (left+int(x)<0 || left+int(x)>=240 || top+int(y)<0 || top+int(y)>=280) continue;
    const uint8_t a = unsigned(text.alpha[y*text.width+x])*brightness/255;
    if (a) fb[(top+y)*240+left+x] = ((a>>3)<<11)|((a>>2)<<5)|(a>>3);
  }
}
// -2: outside, -1: back, 0..2: skins, 3: PC performance, 4: website note, 5: WLAN.
inline int hit(int x, int y) {
  if (y>=62 && y<=194) {
    if (x>=6 && x<=78) return 0;
    if (x>=84 && x<=156) return 1;
    if (x>=162 && x<=234) return 2;
  }
  if (y>=199 && y<=235) {
    if (x>=12 && x<=115) return 5;
    if (x>=125 && x<=228) return 4;
  }
  if (y>=242 && y<=278) {
    if (x>=12 && x<=153) return 3;
    if (x>=163 && x<=228) return -1;
  }
  return -2;
}
inline void draw(Arduino_Canvas& canvas, SagePet& sage, BlackCatPet& blackCat,
                 uint8_t selected) {
  canvas.fillScreen(0);
  label(canvas,MenuLabels::Title,120,9);
  canvas.drawLine(12,35,228,35,0x4208);
  label(canvas,MenuLabels::Hint,120,42,170);
  static constexpr int left[] = {6,84,162};
  static constexpr int center[] = {42,120,198};
  for (int i=0; i<3; ++i) {
    canvas.drawRoundRect(left[i],62,72,132,9,selected==i?0xfecf:0x4208);
    if (selected==i) canvas.drawRoundRect(left[i]+1,63,70,130,8,0xfecf);
  }
  // A compact variant of the existing face: white eyes and a small smile.
  canvas.fillRoundRect(20,92,13,29,6,0xffff);
  canvas.fillRoundRect(51,92,13,29,6,0xffff);
  canvas.drawLine(31,132,42,136,0xffff);
  canvas.drawLine(42,136,53,132,0xffff);
  sage.thumbnail(88,67);
  blackCat.thumbnail(166,67);
  label(canvas,MenuLabels::Mono,42,157);
  label(canvas,MenuLabels::Sage,120,157);
  label(canvas,MenuLabels::Cat,198,157);
  label(canvas,MenuLabels::Selected,center[selected<3?selected:0],179,160);
  canvas.fillRoundRect(12,199,104,37,8,0x1082);
  canvas.drawRoundRect(12,199,104,37,8,0xfecf);
  label(canvas,MenuLabels::Wlan,64,210,240);
  canvas.fillRoundRect(125,199,104,37,8,0x1082);
  canvas.drawRoundRect(125,199,104,37,8,0xfecf);
  label(canvas,MenuLabels::Web,177,210,220);
  canvas.drawRoundRect(12,242,142,37,8,0x4208);
  label(canvas,MenuLabels::Perf,83,252,220);
  canvas.drawRoundRect(163,242,66,37,8,0x4208);
  label(canvas,MenuLabels::Back,196,252,190);
}
}
