#pragma once
#include <stddef.h>
#include <stdint.h>

namespace EspCardTransfer {
enum class Outcome : uint8_t { Complete, Disconnected, IdleTimeout, TotalTimeout, Invalid };
struct Result {
  size_t bytes=0;
  uint32_t elapsedMs=0;
  Outcome outcome=Outcome::Invalid;
};

// Caller supplies its existing destination buffer. NetworkClientSecure can
// return -1 for a temporary gap between TLS records; do not use one readBytes()
// call as proof that an entire HTTP body has arrived. Wait only within limits.
template<class Client,class Clock,class Yield>
Result readExact(Client& stream,uint8_t *target,size_t length,Clock now,Yield yield,
                 uint32_t idleMs=5000,uint32_t totalMs=15000,size_t maxChunk=2048) {
  Result result;
  if (!target || !length || !idleMs || !totalMs || !maxChunk) return result;
  const uint32_t started=now();
  uint32_t lastProgress=started;
  while (result.bytes<length) {
    const uint32_t current=now();
    result.elapsedMs=current-started;
    if (result.elapsedMs>=totalMs) { result.outcome=Outcome::TotalTimeout; return result; }
    if (current-lastProgress>=idleMs) { result.outcome=Outcome::IdleTimeout; return result; }
    const int available=stream.available();
    if (available>0) {
      size_t count=static_cast<size_t>(available);
      if (count>length-result.bytes) count=length-result.bytes;
      if (count>maxChunk) count=maxChunk;
      const int read=stream.read(target+result.bytes,count);
      if (read>0) {
        if (static_cast<size_t>(read)>count) return result;
        result.bytes+=static_cast<size_t>(read);
        lastProgress=now();
        if (result.bytes==length) {
          result.elapsedMs=lastProgress-started;
          result.outcome=Outcome::Complete;
          return result;
        }
      } else if (!stream.connected()) {
        result.elapsedMs=now()-started;
        result.outcome=Outcome::Disconnected;
        return result;
      }
    } else if (!stream.connected()) {
      result.elapsedMs=now()-started;
      result.outcome=Outcome::Disconnected;
      return result;
    }
    yield(); // Also yield between successful chunks: pet UI remains independent.
  }
  return result;
}
}
