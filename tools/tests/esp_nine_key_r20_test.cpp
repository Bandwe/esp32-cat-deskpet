#include <cassert>
#include <cstdio>
#include <cstring>
#include "../../sketch/DeskPetV01/EspNineKeyInput.h"
using namespace EspNineKeyInput;
int main() {
  State s; char field[65]={};
  assert(press(s,field,sizeof(field),2,10) && !std::strcmp(field,"a"));
  assert(press(s,field,sizeof(field),2,100) && !std::strcmp(field,"b"));
  assert(press(s,field,sizeof(field),2,200) && !std::strcmp(field,"c"));
  assert(press(s,field,sizeof(field),2,300) && !std::strcmp(field,"a"));
  assert(!tick(s,1049));
  assert(tick(s,1050) && s.pendingKey==-1);
  assert(!tick(s,1051));
  press(s,field,sizeof(field),2,1100); assert(!std::strcmp(field,"aa"));
  press(s,field,sizeof(field),3,1200); assert(!std::strcmp(field,"aad"));
  press(s,field,sizeof(field),3,1950); assert(!std::strcmp(field,"aadd"));
  caps(s); assert(s.pendingKey==-1 && s.upper);
  press(s,field,sizeof(field),3,1960); assert(!std::strcmp(field,"aaddD"));
  erase(s,field); assert(s.pendingKey==-1 && !std::strcmp(field,"aadd"));
  press(s,field,sizeof(field),3,1970); assert(!std::strcmp(field,"aaddD"));
  setMode(s,Mode::Digits); assert(s.pendingKey==-1);
  press(s,field,sizeof(field),3,1971); press(s,field,sizeof(field),3,1972);
  spaceOrZero(s,field,sizeof(field)); assert(!std::strcmp(field,"aaddD330"));
  nextSymbols(s); assert(s.mode==Mode::Symbols && s.symbolPage==0);
  for (unsigned i=0;i<4;++i) nextSymbols(s);
  assert(s.symbolPage==0);
  setMode(s,Mode::Letters); spaceOrZero(s,field,sizeof(field));
  assert(field[std::strlen(field)-1]==' ');
  // Cycling the current final character remains valid at capacity; no new
  // character can overwrite the terminator or silently replace the old one.
  char tiny[2]={}; s=State{};
  assert(press(s,tiny,sizeof(tiny),2,0));
  assert(press(s,tiny,sizeof(tiny),2,1) && !std::strcmp(tiny,"b"));
  assert(!press(s,tiny,sizeof(tiny),3,2) && !std::strcmp(tiny,"b"));
  assert(s.pendingKey==-1);
  assert(!spaceOrZero(s,tiny,sizeof(tiny)));
  assert(erase(s,tiny) && !tiny[0]);
  assert(!erase(s,tiny));
  assert(!press(s,tiny,sizeof(tiny),0,10));
  assert(!press(s,tiny,sizeof(tiny),10,10));
  // The expiry comparison remains valid across millis() wraparound.
  s=State{}; field[0]=0;
  press(s,field,sizeof(field),2,0xffffff00u);
  assert(!tick(s,0x100u));
  assert(tick(s,0x200u));
  // Every printable ASCII character (32..126), including all password
  // punctuation, is reachable; no predictive text modifies it.
  bool seen[127]={};
  for (unsigned upper=0;upper<2;++upper) for (uint8_t key=1;key<=9;++key) {
    for (size_t choice=0;choice<std::strlen(group(key));++choice) {
      State k; k.upper=upper!=0; char out[2]={};
      for (size_t n=0;n<=choice;++n) assert(press(k,out,sizeof(out),key,10+uint32_t(n)));
      seen[static_cast<unsigned char>(out[0])]=true;
    }
  }
  for (uint8_t key=1;key<=9;++key) {
    State k; k.mode=Mode::Digits; char out[2]={};
    assert(press(k,out,sizeof(out),key,10)); seen[static_cast<unsigned char>(out[0])]=true;
  }
  for (uint8_t page=0;page<SymbolPages;++page) for (uint8_t key=1;key<=9;++key) {
    State k; k.mode=Mode::Symbols; k.symbolPage=page; char out[2]={};
    const bool exists=symbol(page,key)!=0;
    assert(press(k,out,sizeof(out),key,10)==exists);
    if (exists) seen[static_cast<unsigned char>(out[0])]=true;
  }
  s=State{}; field[0]=0; spaceOrZero(s,field,sizeof(field)); seen[' ']=true;
  setMode(s,Mode::Digits); field[0]=0; spaceOrZero(s,field,sizeof(field)); seen['0']=true;
  for (unsigned c=32;c<=126;++c) assert(seen[c]);
  char ssid[33]={}; s=State{}; setMode(s,Mode::Digits);
  for (unsigned i=0;i<32;++i) assert(press(s,ssid,sizeof(ssid),1,i));
  assert(!press(s,ssid,sizeof(ssid),1,33) && std::strlen(ssid)==32);
  field[0]=0;
  for (unsigned i=0;i<64;++i) assert(press(s,field,sizeof(field),1,i));
  assert(!press(s,field,sizeof(field),1,65) && std::strlen(field)==64);
  s=State{}; assert(s.mode==Mode::Letters && !s.upper && s.pendingKey==-1);
  std::puts("PASS: phone nine-key timing, mode/caps/delete, full ASCII, bounds and millis wrap");
}
