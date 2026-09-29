#include "../../sketch/DeskPetV01/WifiNetworkStore.h"
#include <cstdio>
#include <cstdlib>

using namespace WifiNetworkStore;
static unsigned checks=0;
static void check(bool condition,const char *label) {
  ++checks;
  if (!condition) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); }
}
int main() {
  Store data;
  init(data);
  check(valid(data) && data.count==0 && data.preferred==NoIndex,"empty stable record");
  check(sizeof(Store)==627 && sizeof(Legacy)==227,"byte-stable persisted layouts");
  check(upsert(data,"Home","good-pass")==Result::Changed,"add initial network");
  check(valid(data) && data.count==1 && data.preferred==0,"initial network valid");
  strcpy(data.token,"test-token-not-a-secret");
  check(upsert(data,"Home","good-pass")==Result::Unchanged,"unchanged JOIN needs no flash write");
  Store staged=data;
  check(upsert(staged,"Home","bad-pass")==Result::Changed,"password replacement can be staged");
  check(strcmp(data.networks[0].pass,"good-pass")==0,"failed staged attempt preserves old password");
  check(strcmp(staged.token,data.token)==0,"staging preserves pairing token");
  check(upsert(data,"Office","work-pass")==Result::Changed,"add second network");
  check(upsert(data,"Cafe","")==Result::Changed,"allow open network");
  check(upsert(data,"Phone","phone-pass")==Result::Changed,"add fourth network");
  check(upsert(data,"Guest","guest-pass")==Result::Changed && data.count==Capacity,"add fifth network");
  const Store full=data;
  check(upsert(data,"Sixth","sixth-pass")==Result::Full,"exact five network limit");
  check(memcmp(&data,&full,sizeof(data))==0,"full list does not silently evict or modify any entry");
  check(upsert(data,"Office","new-pass")==Result::Changed && data.count==Capacity,"existing update works at capacity");
  check(strcmp(data.networks[0].pass,"good-pass")==0 && strcmp(data.token,full.token)==0,"update preserves unrelated network and token");
  check(prefer(data,"Home")==Result::Changed && data.preferred==0,"select existing SSID");
  check(prefer(data,"Home")==Result::Unchanged,"same preferred SSID causes no write");
  check(prefer(data,"Missing")==Result::NotFound,"selection requires copied existing SSID");
  char selected[33]; strcpy(selected,"Phone");
  check(forget(data,"Office")==Result::Changed,"forget one network");
  check(find(data,selected)==2,"copied SSID resolves after index shift");
  check(prefer(data,selected)==Result::Changed && data.preferred==2,"connect by SSID not stale index");
  check(forget(data,"Home")==Result::Changed && data.preferred==1,"preferred shifts with preceding deletion");
  check(forget(data,"Phone")==Result::Changed && data.preferred==0,"preferred deletion chooses remaining entry");
  check(forget(data,"Missing")==Result::NotFound,"missing forget is nondestructive");
  check(forget(data,"Cafe")==Result::Changed && forget(data,"Guest")==Result::Changed,"forget all WLANs");
  check(valid(data) && data.count==0 && data.preferred==NoIndex,"empty store remains valid after forget all");
  check(strcmp(data.token,"test-token-not-a-secret")==0,"forget all retains website pairing");
  const Network empty={};
  for (uint8_t i=0;i<Capacity;++i) check(memcmp(&data.networks[i],&empty,sizeof(empty))==0,"removed secret bytes wiped");

  Legacy legacy={};
  strcpy(legacy.ssid,"LegacyNet"); strcpy(legacy.pass,"legacy-pass"); strcpy(legacy.token,"legacy-token-not-secret");
  const Legacy original=legacy;
  check(migrate(legacy,data) && valid(data) && data.count==1,"legacy single-network migration");
  check(strcmp(data.networks[0].ssid,legacy.ssid)==0 && strcmp(data.networks[0].pass,legacy.pass)==0,"migration retains credentials");
  check(strcmp(data.token,legacy.token)==0 && memcmp(&legacy,&original,sizeof(legacy))==0,"migration retains token and source record");
  check(clear(data)==Result::Changed && valid(data) && !data.count,"explicit clear removes saved WLANs");
  check(strcmp(data.token,legacy.token)==0,"explicit clear preserves website token");
  for (uint8_t i=0;i<Capacity;++i) check(memcmp(&data.networks[i],&empty,sizeof(empty))==0,"clear wipes every credential slot");
  check(clear(data)==Result::Unchanged,"repeated clear is idempotent");
  legacy.ssid[0]=legacy.pass[0]=0;
  check(migrate(legacy,data) && !data.count && strcmp(data.token,legacy.token)==0,"token independent of Wi-Fi count");
  memset(legacy.ssid,'a',sizeof(legacy.ssid));
  check(!migrate(legacy,data),"reject unterminated legacy SSID");
  init(data);
  check(upsert(data,"","")==Result::Invalid && upsert(data,"X","short")==Result::Invalid,"reject malformed inputs");
  check(upsert(data,nullptr,"")==Result::Invalid && upsert(data,"X",nullptr)==Result::Invalid,"reject null input");
  char longSsid[34]; memset(longSsid,'s',33); longSsid[33]=0;
  check(upsert(data,longSsid,"")==Result::Invalid,"SSID byte limit");
  char maximumSsid[33]; memset(maximumSsid,'s',32); maximumSsid[32]=0;
  char maximumPsk[65]; memset(maximumPsk,'a',64); maximumPsk[64]=0;
  check(upsert(data,maximumSsid,maximumPsk)==Result::Changed && valid(data),"maximum SSID and hex PSK");
  maximumPsk[0]='z';
  check(upsert(data,"InvalidPSK",maximumPsk)==Result::Invalid,"64 byte PSK must be hex");
  check(upsert(data,"\xe5\xae\xb6\xe9\x87\x8c","unicode-pass")==Result::Changed,"UTF-8 SSID bytes preserved");
  const Store validData=data;
  data.magic[0]='X'; check(!valid(data),"reject wrong record magic"); data=validData;
  data.version=99; check(!valid(data),"reject unsupported record version"); data=validData;
  data.count=Capacity+1; check(!valid(data),"reject corrupt count"); data=validData;
  data.preferred=NoIndex; check(!valid(data),"reject corrupt preferred index"); data=validData;
  data.networks[1]=data.networks[0]; check(!valid(data),"reject duplicate persisted SSIDs"); data=validData;
  memset(data.networks[0].pass,'a',sizeof(data.networks[0].pass)); check(!valid(data),"reject unterminated persisted password"); data=validData;
  memset(data.token,'a',sizeof(data.token)); check(!valid(data),"reject unterminated persisted token"); data=validData;
  strcpy(data.token,"short"); check(!valid(data),"reject short persisted token");
  std::printf("PASS: %u WLAN store checks; no serial, network or NVS writes.\n",checks);
}
