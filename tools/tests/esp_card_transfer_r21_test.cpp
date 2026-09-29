#include "../../sketch/DeskPetV01/EspCardTransfer.h"
#include "../../sketch/DeskPetV01/EspMessageProtocol.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static unsigned checks=0;
static void check(bool condition,const char *name) {
  ++checks;
  if (!condition) { std::fprintf(stderr,"FAIL %s\n",name); std::exit(1); }
}
struct Packet { uint32_t at; size_t end; };
struct FakeClient {
  uint32_t elapsed=0, origin=0, closeAt=UINT32_MAX;
  size_t offset=0, maxRequested=0;
  unsigned waits=0, emptyReads=0;
  bool transientReadFailure=false;
  std::vector<uint8_t> payload;
  std::vector<Packet> packets;
  explicit FakeClient(size_t count) : payload(count) {
    for (size_t i=0;i<count;++i) payload[i]=static_cast<uint8_t>((i*17u+31u)&255u);
  }
  int available() {
    size_t end=0;
    for (const auto& packet:packets) if (elapsed>=packet.at) end=packet.end;
    return end>offset?static_cast<int>(end-offset):0;
  }
  int read(uint8_t *target,size_t length) {
    maxRequested=std::max(maxRequested,length);
    if (transientReadFailure) { transientReadFailure=false; ++emptyReads; return -1; }
    const int count=available();
    if (count<=0) { ++emptyReads; return -1; } // Same temporary-gap behavior as the TLS client.
    const size_t n=std::min(length,static_cast<size_t>(count));
    std::memcpy(target,payload.data()+offset,n);
    offset+=n;
    return static_cast<int>(n);
  }
  bool connected() { return elapsed<closeAt; }
  uint32_t now() const { return origin+elapsed; }
  void wait() { ++waits; ++elapsed; }
};
static EspCardTransfer::Result receive(FakeClient& stream,std::vector<uint8_t>& target,
                                        uint32_t idle=5000,uint32_t total=15000,size_t chunk=2048) {
  return EspCardTransfer::readExact(stream,target.data(),target.size(),[&](){return stream.now();},
    [&](){stream.wait();},idle,total,chunk);
}
int main() {
  using EspCardTransfer::Outcome;
  constexpr size_t bytes=EspMessageProtocol::CardBytes;
  std::vector<uint8_t> target(bytes);
  FakeClient exact(bytes); exact.packets={{0,bytes}};
  auto result=receive(exact,target);
  check(result.outcome==Outcome::Complete && result.bytes==bytes,"exact full body");
  check(target==exact.payload,"exact payload bytes preserved");
  check(exact.maxRequested<=2048 && exact.waits>0,"bounded chunks yield even with data ready");

  FakeClient gaps(bytes); gaps.packets={{0,1387},{8,4096},{35,16384},{95,bytes}};
  result=receive(gaps,target);
  check(result.outcome==Outcome::Complete && result.bytes==bytes,"TLS records separated by gaps complete");
  check(target==gaps.payload && gaps.emptyReads==0,"wait for availability instead of treating a gap as EOF");
  check(result.elapsedMs>=95 && gaps.waits>=95,"waited and yielded for later packets");

  FakeClient legacy(bytes); legacy.packets=gaps.packets;
  size_t legacyCount=0;
  while (legacyCount<bytes) {
    const int n=legacy.read(target.data()+legacyCount,bytes-legacyCount);
    if (n<0) break;
    legacyCount+=static_cast<size_t>(n);
  }
  check(legacyCount==1387 && legacyCount<bytes,"old readBytes loop reproduces gap short-read");

  FakeClient transient(bytes); transient.packets={{0,bytes}}; transient.transientReadFailure=true;
  result=receive(transient,target);
  check(result.outcome==Outcome::Complete && transient.emptyReads==1,"transient negative read tolerated while connected");

  FakeClient dropped(bytes); dropped.packets={{0,bytes/2}}; dropped.closeAt=30;
  result=receive(dropped,target);
  check(result.outcome==Outcome::Disconnected && result.bytes==bytes/2,"half-body disconnect rejected");
  check(result.elapsedMs==30,"disconnect returns promptly");

  FakeClient stalled(bytes); stalled.packets={{0,123}};
  result=receive(stalled,target,50,500);
  check(result.outcome==Outcome::IdleTimeout && result.bytes==123,"stall is not successful short payload");
  check(result.elapsedMs==50,"idle timeout bounded");

  FakeClient empty(bytes);
  result=receive(empty,target,40,500);
  check(result.outcome==Outcome::IdleTimeout && !result.bytes && result.elapsedMs==40,"no first byte has bounded wait");

  FakeClient trickle(bytes);
  for (uint32_t i=0;i<20;++i) trickle.packets.push_back({i*20,static_cast<size_t>(i+1)});
  result=receive(trickle,target,50,150);
  check(result.outcome==Outcome::TotalTimeout && result.elapsedMs==150,"continuous trickle bounded by total timeout");
  check(result.bytes==8,"total timeout counts partial bytes accurately");

  FakeClient wrap(bytes); wrap.origin=UINT32_MAX-20; wrap.packets={{0,10},{30,bytes}};
  result=receive(wrap,target,50,500);
  check(result.outcome==Outcome::Complete && result.elapsedMs>=30,"millis wraparound supported");

  FakeClient extra(bytes+100); extra.packets={{0,bytes+100}};
  result=receive(extra,target);
  check(result.outcome==Outcome::Complete && extra.offset==bytes,"never reads beyond requested body");
  check(extra.available()==100,"subsequent bytes left unread");

  FakeClient tiny(bytes); tiny.packets={{0,bytes}};
  result=receive(tiny,target,5000,15000,127);
  check(result.outcome==Outcome::Complete && tiny.maxRequested<=127,"custom chunk limit obeyed");

  FakeClient invalid(bytes);
  result=receive(invalid,target,0,500);
  check(result.outcome==Outcome::Invalid && !invalid.waits,"invalid timeout rejected without reads");
  result=EspCardTransfer::readExact(invalid,static_cast<uint8_t*>(nullptr),bytes,[&](){return invalid.now();},[&](){invalid.wait();});
  check(result.outcome==Outcome::Invalid,"null destination rejected");

  FakeClient checked(bytes); checked.packets={{0,1000},{20,bytes}};
  result=receive(checked,target);
  const uint32_t expected=EspMessageProtocol::crc32(checked.payload.data(),bytes);
  check(result.outcome==Outcome::Complete && EspMessageProtocol::crc32(target.data(),bytes)==expected,"full-body CRC matches after packet gaps");
  target[bytes/2]^=0x01;
  check(EspMessageProtocol::crc32(target.data(),bytes)!=expected,"same-size corruption fails CRC");
  check(EspMessageProtocol::crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xCBF43926u,"CRC32 standard test vector");
  std::printf("PASS: %u card-transfer checks; reproduced old TLS gap short-read.\n",checks);
}
