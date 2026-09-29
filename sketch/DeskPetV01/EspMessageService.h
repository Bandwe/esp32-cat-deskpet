#pragma once
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "EspMessageProtocol.h"
#include "EspProvisionInput.h"

namespace EspMessageService {
enum class PairStatus : uint8_t { Idle, Waiting, Busy, Success, Invalid, NewCode, Offline, Failed, SaveFailed, TokenRejected };
inline const char* pairStatusName(PairStatus pair) {
  switch (pair) {
    case PairStatus::Waiting: return "waiting";
    case PairStatus::Busy: return "busy";
    case PairStatus::Success: return "success";
    case PairStatus::Invalid: return "invalid";
    case PairStatus::NewCode: return "new_code";
    case PairStatus::Offline: return "offline";
    case PairStatus::Failed: return "failed";
    case PairStatus::SaveFailed: return "save_failed";
    case PairStatus::TokenRejected: return "token_rejected";
    default: return "idle";
  }
}
struct Snapshot {
  bool ready = false, configured = false, wifiSaved = false, wifi = false, clock = false;
  bool server = false, portal = false, hasCard = false;
  bool scanning = false;
  uint8_t scanCount = 0;
  uint32_t scanGeneration = 0;
  char scanSsids[12][33] = {};
  int8_t scanRssi[12] = {};
  bool scanOpen[12] = {};
  uint8_t scanAuth[12] = {};
  bool scanSaved[12] = {};
  uint8_t savedCount = 0;
  char savedSsids[5][33] = {};
  char connectedSsid[33] = {}, connectingSsid[33] = {}, ip[16] = {};
  int8_t rssi = 0;
  uint32_t wifiGeneration = 0;
  bool wifiBusy = false;
  PairStatus pair = PairStatus::Idle;
  uint32_t revision = 0, dismissed = 0, receivedAtMs = 0;
  uint32_t httpRequests = 0, warmResponses = 0;
  int cardHttpCode = 0, cardContentLength = 0;
  uint32_t cardReadBytes = 0, cardExpectedCrc = 0, cardActualCrc = 0, cardTransferMs = 0;
  uint64_t tokenId = 0; // Local non-secret discriminator for dismiss state.
  char apSsid[28] = {}, apPassword[20] = {};
  char error[20] = {};
};

// All TLS, HTTP and SoftAP/web-server work runs on a separate FreeRTOS task.
// Only small snapshots and verified card-pointer swaps cross to the UI loop.
bool begin();
Snapshot snapshot();
void requestProvision();
void cancelProvision();
bool requestScan();
bool saveWifi(const char *ssid,const char *password);
bool connectSaved(const char *ssid);
// The UI obtains confirmation before calling this; SSID is copied on queueing.
bool forgetWifi(const char *ssid);
// Explicit one-shot reset of WLAN credentials/cache only; never called on boot.
bool clearWifi();
bool requestPair(const char *code);
void dismiss(uint32_t revision);
bool drawCard(Arduino_Canvas& canvas, uint32_t& revision);
}
