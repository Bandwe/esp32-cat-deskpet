#pragma once
#include <array>
#include <cstdint>

// The actual cat renderer writes through getFramebuffer(); this host canvas
// exposes the same 240 x 280 pixel surface without any LCD or USB access.
class Arduino_Canvas {
 public:
  std::array<uint16_t,240*280> pixels = {};
  uint16_t* getFramebuffer() { return pixels.data(); }
  void fillScreen(uint16_t color) { pixels.fill(color); }
  void fillCircle(int cx,int cy,int radius,uint16_t color) {
    for (int y=cy-radius; y<=cy+radius; ++y) {
      if (y<0 || y>=280) continue;
      for (int x=cx-radius; x<=cx+radius; ++x) {
        if (x<0 || x>=240) continue;
        const int dx=x-cx,dy=y-cy;
        if (dx*dx+dy*dy<=radius*radius) pixels[y*240+x]=color;
      }
    }
  }
};
