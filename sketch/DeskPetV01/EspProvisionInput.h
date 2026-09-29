#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace EspProvisionInput {
static constexpr uint8_t Columns=6, Rows=5;
static constexpr int KeyTop=70, KeyHeight=32, KeyWidth=40;
enum class Page : uint8_t { Letters, Numbers, More };

inline bool validSsid(const char *ssid) {
  if (!ssid) return false;
  const size_t n=strlen(ssid);
  return n>0 && n<=32;
}
inline bool validPassword(const char *pass) {
  if (!pass) return false;
  const size_t n=strlen(pass);
  if (!n) return true; // Open network.
  if (n<8 || n>64) return false;
  for (size_t i=0;i<n;++i) {
    const unsigned char c=static_cast<unsigned char>(pass[i]);
    if (n==64) {
      if (!((c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F'))) return false;
    } else if (c<32 || c>126) return false;
  }
  return true;
}
inline bool validCode(const char *code) {
  if (!code || strlen(code)!=8) return false;
  for (unsigned i=0;i<8;++i) if (code[i]<'0' || code[i]>'9') return false;
  return true;
}
inline bool sameScanEntry(const char *ssidA,uint8_t authA,const char *ssidB,uint8_t authB) {
  return ssidA && ssidB && authA==authB && strcmp(ssidA,ssidB)==0;
}
inline bool append(char *target,size_t capacity,char value) {
  const size_t n=strlen(target);
  if (!value || n+1>=capacity) return false;
  target[n]=value; target[n+1]=0;
  return true;
}
inline void erase(char *target) {
  const size_t n=strlen(target);
  if (n) target[n-1]=0;
}
inline char key(uint8_t row,uint8_t col,Page page,bool upper) {
  if (row>=Rows || col>=Columns) return 0;
  static constexpr char letters[Rows][Columns+1]={
    "qwerty", "uiopas", "dfghjk", "lzxcvb", "nm_-. "
  };
  static constexpr char numbers[Rows][Columns+1]={
    "123456", "7890!@", "#$%^&*", "()[]{}", "/\\|:;?"
  };
  static constexpr char more[Rows][Columns+1]={
    "\"',+=<", ">\x60~", "", "", ""
  };
  char c=(page==Page::Letters?letters:(page==Page::Numbers?numbers:more))[row][col];
  if (upper && c>='a' && c<='z') c-=32;
  return c;
}
inline char keyAt(int x,int y,Page page,bool upper) {
  if (x<0 || x>=240 || y<KeyTop || y>=KeyTop+Rows*KeyHeight) return 0;
  return key((y-KeyTop)/KeyHeight,x/KeyWidth,page,upper);
}
}
