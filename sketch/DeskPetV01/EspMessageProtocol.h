#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

namespace EspMessageProtocol {
static constexpr uint16_t Width = 240, Height = 280;
static constexpr size_t CardBytes = size_t(Width) * Height / 2;
struct Latest { uint32_t revision = 0, cardBytes = 0, crc32 = 0; };

inline uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (unsigned bit = 0; bit < 8; ++bit)
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xEDB88320u : 0u);
  }
  return ~crc;
}

inline bool space(char c) { return c==' ' || c=='\n' || c=='\r' || c=='\t'; }
inline void skipSpace(const char *&p) { while (space(*p)) ++p; }
inline bool parseUint(const char *&p, uint32_t& out) {
  if (*p<'0' || *p>'9') return false;
  uint32_t n = 0;
  do {
    const unsigned digit = *p++ - '0';
    if (n > (UINT32_MAX-digit)/10u) return false;
    n = n*10u+digit;
  } while (*p>='0' && *p<='9');
  out = n;
  return true;
}

// Strict bounded parser for the three-integer server contract. Rejects
// duplicate, negative, overflowed and inconsistent metadata before download.
inline bool parseLatest(const char *json, Latest& out) {
  if (!json) return false;
  const char *p = json;
  skipSpace(p);
  if (*p++ != '{') return false;
  Latest result;
  unsigned seen = 0;
  for (unsigned i = 0; i < 3; ++i) {
    skipSpace(p);
    if (*p++ != '"') return false;
    const char *key = p;
    while (*p && *p != '"' && size_t(p-key) <= 20) ++p;
    if (*p != '"' || size_t(p-key) > 20) return false;
    const size_t keyLength = p-key;
    ++p;
    skipSpace(p);
    if (*p++ != ':') return false;
    skipSpace(p);
    uint32_t value = 0;
    if (!parseUint(p,value)) return false;
    unsigned field = 0;
    if (keyLength==8 && !memcmp(key,"revision",8)) { field=1; result.revision=value; }
    else if (keyLength==10 && !memcmp(key,"card_bytes",10)) { field=2; result.cardBytes=value; }
    else if (keyLength==10 && !memcmp(key,"card_crc32",10)) { field=4; result.crc32=value; }
    else return false;
    if (seen & field) return false;
    seen |= field;
    skipSpace(p);
    if (i<2) { if (*p++ != ',') return false; }
  }
  skipSpace(p);
  if (*p++ != '}') return false;
  skipSpace(p);
  if (*p != 0 || seen != 7) return false;
  if ((result.revision==0 && (result.cardBytes || result.crc32)) ||
      (result.revision>0 && result.cardBytes!=CardBytes)) return false;
  out = result;
  return true;
}

// Pairing has exactly one JSON string field. Keeping this parser narrow avoids
// accepting a truncated body or an escaped/control-character credential.
inline bool parsePairToken(const char *json, char (&token)[129]) {
  if (!json) return false;
  const char *p=json;
  skipSpace(p);
  if (*p++!='{') return false;
  skipSpace(p);
  static constexpr char key[]="\"deviceToken\"";
  if (strncmp(p,key,sizeof(key)-1)!=0) return false;
  p+=sizeof(key)-1;
  skipSpace(p);
  if (*p++!=':') return false;
  skipSpace(p);
  if (*p++!='\"') return false;
  char candidate[129]={};
  size_t n=0;
  while (*p && *p!='\"') {
    const unsigned char c=static_cast<unsigned char>(*p++);
    if (c<33 || c>126 || c=='\\' || n>=128) return false;
    candidate[n++]=static_cast<char>(c);
  }
  if (*p++!='\"' || n<16) return false;
  skipSpace(p);
  if (*p++!='}') return false;
  skipSpace(p);
  if (*p) return false;
  memcpy(token,candidate,n+1);
  return true;
}
}
