#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../../sketch/DeskPetV01/EspMessageProtocol.h"
int main() {
  using namespace EspMessageProtocol;
  Latest latest;
  assert(parseLatest("{\"revision\":0,\"card_bytes\":0,\"card_crc32\":0}",latest));
  assert(latest.revision==0);
  assert(parseLatest("{ \"card_crc32\" : 4294967295, \"card_bytes\": 33600, \"revision\": 2 }",latest));
  assert(latest.revision==2 && latest.crc32==UINT32_MAX);
  assert(!parseLatest("{\"revision\":2,\"card_bytes\":33601,\"card_crc32\":1}",latest));
  assert(!parseLatest("{\"revision\":-1,\"card_bytes\":0,\"card_crc32\":0}",latest));
  assert(!parseLatest("{\"revision\":1,\"revision\":1,\"card_crc32\":0}",latest));
  assert(!parseLatest("{\"revision\":1,\"card_bytes\":33600,\"card_crc32\":4294967296}",latest));
  assert(!parseLatest("{\"revision\":1,\"card_bytes\":33600,\"card_crc32\":0}garbage",latest));
  const char *check = "123456789";
  assert(crc32(reinterpret_cast<const uint8_t*>(check),strlen(check))==0xCBF43926u);
}
