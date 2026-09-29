#pragma once
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// Host-only drawing recorder. No firmware dependency; useful for route tests
// and 240 x 280 layout previews generated from the actual UI draw functions.
class Arduino_Canvas {
 public:
  std::vector<std::string> labels;
  std::string shapes;
  int cursorX=0,cursorY=0;
  unsigned fontSize=1;
  uint16_t textColor=0xffff;
  static std::string color(uint16_t c) {
    char out[10];
    std::snprintf(out,sizeof(out),"#%02x%02x%02x",((c>>11)&31)*255/31,((c>>5)&63)*255/63,(c&31)*255/31);
    return out;
  }
  static std::string escaped(const char* s) {
    std::string out;
    for (;*s;++s) {
      if (*s=='&') out+="&amp;";
      else if (*s=='<') out+="&lt;";
      else if (*s=='>') out+="&gt;";
      else if (*s=='\"') out+="&quot;";
      else out+=*s;
    }
    return out;
  }
  void setTextColor(uint16_t c) { textColor=c; }
  void setTextSize(unsigned n) { fontSize=n; }
  void setCursor(int x,int y) { cursorX=x; cursorY=y; }
  void print(const char* s) {
    labels.emplace_back(s);
    if (!*s) return;
    shapes+="<text x=\""+std::to_string(cursorX)+"\" y=\""+std::to_string(cursorY+7*fontSize)+
      "\" fill=\""+color(textColor)+"\" font-family=\"monospace\" font-size=\""+std::to_string(8*fontSize)+
      "\" textLength=\""+std::to_string(std::string(s).size()*6*fontSize)+"\" lengthAdjust=\"spacingAndGlyphs\">"+escaped(s)+"</text>\n";
  }
  void rect(int x,int y,int w,int h,int r,uint16_t c,bool fill) {
    shapes+="<rect x=\""+std::to_string(x)+"\" y=\""+std::to_string(y)+"\" width=\""+std::to_string(w)+
      "\" height=\""+std::to_string(h)+"\" rx=\""+std::to_string(r)+"\" fill=\""+(fill?color(c):"none")+
      "\" stroke=\""+(fill?"none":color(c))+"\"/>\n";
  }
  void fillScreen(uint16_t c) { shapes.clear(); labels.clear(); rect(0,0,240,280,0,c,true); }
  void fillRoundRect(int x,int y,int w,int h,int r,uint16_t c) { rect(x,y,w,h,r,c,true); }
  void drawRoundRect(int x,int y,int w,int h,int r,uint16_t c) { rect(x,y,w,h,r,c,false); }
  void fillCircle(int x,int y,int r,uint16_t c) {
    shapes+="<circle cx=\""+std::to_string(x)+"\" cy=\""+std::to_string(y)+"\" r=\""+std::to_string(r)+"\" fill=\""+color(c)+"\"/>\n";
  }
  void drawLine(int x,int y,int x2,int y2,uint16_t c) {
    shapes+="<path d=\"M"+std::to_string(x)+","+std::to_string(y)+" L"+std::to_string(x2)+","+std::to_string(y2)+"\" stroke=\""+color(c)+"\"/>\n";
  }
  void saveSvg(const std::string& path) const {
    std::ofstream f(path);
    f<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"240\" height=\"280\" viewBox=\"0 0 240 280\">\n"<<shapes<<"</svg>\n";
  }
};
