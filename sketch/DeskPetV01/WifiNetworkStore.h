#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "EspProvisionInput.h"

// Original, allocation-free persisted model. A complete record is committed to
// one NVS blob; the website token is independent of the number of saved WLANs.
namespace WifiNetworkStore {
static constexpr uint8_t Capacity=5, NoIndex=255;
struct Network { char ssid[33], pass[65]; };
struct Legacy { char ssid[33], pass[65], token[129]; };
struct Store {
  char magic[4];
  uint8_t version, count, preferred, reserved;
  Network networks[Capacity];
  char token[129];
};
static_assert(sizeof(Legacy)==227,"Legacy credential layout changed");
static_assert(sizeof(Store)==627,"WLAN record must have a stable byte layout");
enum class Result : uint8_t { Changed, Unchanged, Full, Invalid, NotFound };

inline void init(Store& data) {
  memset(&data,0,sizeof(data));
  memcpy(data.magic,"DPW2",4);
  data.version=1;
  data.preferred=NoIndex;
}
inline bool terminated(const char *value,size_t capacity) {
  return value && memchr(value,0,capacity)!=nullptr;
}
inline bool validToken(const char *token) {
  if (!terminated(token,129)) return false;
  const size_t length=strlen(token);
  if (!length) return true;
  if (length<16 || length>128) return false;
  for (size_t i=0;i<length;++i)
    if (static_cast<unsigned char>(token[i])<33 || static_cast<unsigned char>(token[i])>126) return false;
  return true;
}
inline int find(const Store& data,const char *ssid) {
  if (!ssid || data.count>Capacity) return -1;
  for (uint8_t i=0;i<data.count;++i)
    if (strcmp(data.networks[i].ssid,ssid)==0) return i;
  return -1;
}
inline bool valid(const Store& data) {
  if (memcmp(data.magic,"DPW2",4)!=0 || data.version!=1 || data.reserved!=0 ||
      data.count>Capacity || !validToken(data.token)) return false;
  if (data.count ? data.preferred>=data.count : data.preferred!=NoIndex) return false;
  for (uint8_t i=0;i<data.count;++i) {
    const Network& network=data.networks[i];
    if (!terminated(network.ssid,sizeof(network.ssid)) || !terminated(network.pass,sizeof(network.pass)) ||
        !EspProvisionInput::validSsid(network.ssid) || !EspProvisionInput::validPassword(network.pass)) return false;
    for (uint8_t j=0;j<i;++j)
      if (strcmp(network.ssid,data.networks[j].ssid)==0) return false;
  }
  return true;
}
inline Result prefer(Store& data,const char *ssid) {
  const int index=find(data,ssid);
  if (index<0) return Result::NotFound;
  if (data.preferred==index) return Result::Unchanged;
  data.preferred=static_cast<uint8_t>(index);
  return Result::Changed;
}
inline Result upsert(Store& data,const char *ssid,const char *password) {
  if (!EspProvisionInput::validSsid(ssid) || !EspProvisionInput::validPassword(password)) return Result::Invalid;
  int index=find(data,ssid);
  if (index<0) {
    if (data.count>=Capacity) return Result::Full;
    index=data.count++;
    memset(&data.networks[index],0,sizeof(Network));
    strcpy(data.networks[index].ssid,ssid);
    strcpy(data.networks[index].pass,password);
    data.preferred=static_cast<uint8_t>(index);
    return Result::Changed;
  }
  if (strcmp(data.networks[index].pass,password)==0 && data.preferred==index) return Result::Unchanged;
  memset(data.networks[index].pass,0,sizeof(data.networks[index].pass));
  strcpy(data.networks[index].pass,password);
  data.preferred=static_cast<uint8_t>(index);
  return Result::Changed;
}
inline Result forget(Store& data,const char *ssid) {
  const int index=find(data,ssid);
  if (index<0) return Result::NotFound;
  for (uint8_t i=static_cast<uint8_t>(index); i+1<data.count; ++i) data.networks[i]=data.networks[i+1];
  --data.count;
  memset(&data.networks[data.count],0,sizeof(Network));
  if (!data.count) data.preferred=NoIndex;
  else if (data.preferred==index) data.preferred=0;
  else if (data.preferred>index) --data.preferred;
  return Result::Changed;
}
inline Result clear(Store& data) {
  const bool changed=data.count!=0;
  memset(data.networks,0,sizeof(data.networks));
  data.count=0;
  data.preferred=NoIndex;
  return changed?Result::Changed:Result::Unchanged;
}
inline bool migrate(const Legacy& old,Store& data) {
  init(data);
  if (!terminated(old.ssid,sizeof(old.ssid)) || !terminated(old.pass,sizeof(old.pass)) || !validToken(old.token)) return false;
  // A paired but WLAN-less record remains paired after migration.
  strcpy(data.token,old.token);
  if (!old.ssid[0]) return true;
  return upsert(data,old.ssid,old.pass)==Result::Changed;
}
}
