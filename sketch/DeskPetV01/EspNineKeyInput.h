#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Original, small phone-style multi-tap input. No dictionary or auto-correction:
// each byte entered is exactly the ASCII character selected by the user.
namespace EspNineKeyInput {
static constexpr uint32_t CommitMs=750;
static constexpr uint8_t SymbolPages=4;
enum class Mode : uint8_t { Letters, Digits, Symbols };
struct State {
  Mode mode=Mode::Letters;
  uint8_t symbolPage=0, choice=0;
  bool upper=false;
  int8_t pendingKey=-1;
  size_t pendingPosition=0;
  uint32_t lastAt=0;
};
inline const char* group(uint8_t key) {
  static const char* const groups[9]={".,?!","abc","def","ghi","jkl","mno","pqrs","tuv","wxyz"};
  return key>=1 && key<=9?groups[key-1]:"";
}
inline char symbol(uint8_t page,uint8_t key) {
  static constexpr char symbols[]="!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~";
  const size_t i=size_t(page)*9+key-1;
  return page<SymbolPages && key>=1 && key<=9 && i<sizeof(symbols)-1?symbols[i]:0;
}
inline void commit(State& s) { s.pendingKey=-1; s.choice=0; s.pendingPosition=0; s.lastAt=0; }
inline bool tick(State& s,uint32_t now) {
  if (s.pendingKey>=1 && uint32_t(now-s.lastAt)>=CommitMs) { commit(s); return true; }
  return false;
}
inline char applyCase(char c,bool upper) { return upper && c>='a' && c<='z'?c-'a'+'A':c; }
inline bool append(char* field,size_t capacity,char c) {
  if (!field || !capacity || !c) return false;
  const size_t n=strlen(field);
  if (n+1>=capacity) return false;
  field[n]=c; field[n+1]=0; return true;
}
inline bool press(State& s,char* field,size_t capacity,uint8_t key,uint32_t now) {
  tick(s,now);
  if (!field || !capacity || key<1 || key>9) return false;
  if (s.mode!=Mode::Letters) {
    commit(s);
    return append(field,capacity,s.mode==Mode::Digits?char('0'+key):symbol(s.symbolPage,key));
  }
  const char* letters=group(key);
  const size_t n=strlen(field);
  if (s.pendingKey==key && n && s.pendingPosition==n-1) {
    s.choice=(s.choice+1)%strlen(letters);
    field[s.pendingPosition]=applyCase(letters[s.choice],s.upper);
    s.lastAt=now;
    return true;
  }
  commit(s);
  if (!append(field,capacity,applyCase(letters[0],s.upper))) return false;
  s.pendingKey=static_cast<int8_t>(key); s.pendingPosition=n; s.lastAt=now;
  return true;
}
inline void setMode(State& s,Mode mode) { commit(s); s.mode=mode; }
inline void nextSymbols(State& s) {
  commit(s);
  s.symbolPage=s.mode==Mode::Symbols?(s.symbolPage+1)%SymbolPages:0;
  s.mode=Mode::Symbols;
}
inline void caps(State& s) { commit(s); s.upper=!s.upper; }
inline bool spaceOrZero(State& s,char* field,size_t capacity) {
  commit(s); return append(field,capacity,s.mode==Mode::Digits?'0':' ');
}
inline bool erase(State& s,char* field) {
  commit(s);
  if (!field) return false;
  const size_t n=strlen(field);
  if (!n) return false;
  field[n-1]=0; return true;
}
}
