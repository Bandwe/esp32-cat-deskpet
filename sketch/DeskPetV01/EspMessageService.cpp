#include "EspMessageService.h"
#include "DeskPetConfig.h"
#include "IsrgRootX1.h"
#include "WifiNetworkStore.h"
#include "EspCardTransfer.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <time.h>

namespace EspMessageService {
namespace {
static constexpr char Host[] = DESKPET_MESSAGE_HOST;
static constexpr uint32_t PollMs = 2000, PortalMs = 300000;
static constexpr uint32_t ConnectMs = 15000, RetryPauseMs = 20000, ScanMs = 12000;
static constexpr char StoreKey[] = "networks-v2";
static constexpr time_t SafeEpoch = 1700000000; // Validates TLS dates.
SemaphoreHandle_t guard = nullptr;
TaskHandle_t workerHandle = nullptr;
Preferences saved;
Snapshot state;
WifiNetworkStore::Store networks;
bool storePersisted=false;
struct DismissState { uint64_t tokenFingerprint; uint32_t revision; };
uint8_t *front = nullptr, *back = nullptr;
enum class WifiCommand : uint8_t { None, Save, Connect, Forget, Clear };
bool wantPortal = false, wantCancel = false, wantScan = false, wantPair = false;
bool clearQueued=false;
WifiCommand wantedWifi = WifiCommand::None;
WebServer portal(80);
bool portalRoutesReady = false, portalSubmitted = false;
char pendingSsid[33] = {}, pendingPass[65] = {}, pendingToken[129] = {};
char requestedSsid[33] = {}, requestedPass[65] = {}, requestedCode[9] = {};
char currentSsid[33] = {}, currentPass[65] = {}, currentToken[129] = {};
char portalNonce[17] = {};
uint32_t portalStarted = 0;

bool take(uint32_t ms = 20) { return guard && xSemaphoreTake(guard,pdMS_TO_TICKS(ms))==pdTRUE; }
void give() { xSemaphoreGive(guard); }
uint64_t tokenFingerprint(const char *token);
void error(const char *reason) {
  if (!take()) return;
  if (strcmp(state.error,reason)!=0) ++state.wifiGeneration;
  strncpy(state.error,reason,sizeof(state.error)-1);
  state.error[sizeof(state.error)-1]=0;
  state.server = false;
  give();
}
void status(bool wifi, bool clock, bool server) {
  if (!take()) return;
  bool changed=state.wifi!=wifi || state.clock!=clock || state.server!=server;
  state.wifi=wifi; state.clock=clock; state.server=server;
  if (wifi && (!strncmp(state.error,"WLAN",4) || !strncmp(state.error,"SSID",4) ||
               (clock && !strncmp(state.error,"CLOCK",5)))) {
    state.error[0]=0;
    changed=true;
  }
  if (changed) ++state.wifiGeneration;
  give();
}
void publishSavedLocked() {
  state.savedCount=networks.count;
  memset(state.savedSsids,0,sizeof(state.savedSsids));
  for (uint8_t i=0;i<networks.count;++i) strcpy(state.savedSsids[i],networks.networks[i].ssid);
  for (uint8_t i=0;i<state.scanCount;++i) state.scanSaved[i]=WifiNetworkStore::find(networks,state.scanSsids[i])>=0;
  state.wifiSaved=networks.count!=0;
  state.configured=currentToken[0]!=0;
  ++state.wifiGeneration;
}
bool commitStore(const WifiNetworkStore::Store& replacement) {
  if (!WifiNetworkStore::valid(replacement) || !take(1000)) return false;
  if (storePersisted && memcmp(&networks,&replacement,sizeof(networks))==0) { give(); return true; }
  const bool stored=saved.putBytes(StoreKey,&replacement,sizeof(replacement))==sizeof(replacement);
  if (stored) {
    const bool changedToken=strcmp(currentToken,replacement.token)!=0;
    networks=replacement;
    storePersisted=true;
    strcpy(currentToken,networks.token);
    if (changedToken) {
      state.hasCard=false;
      state.revision=state.dismissed=0;
      state.tokenId=tokenFingerprint(currentToken);
      const DismissState blank={state.tokenId,0};
      saved.putBytes("dismiss",&blank,sizeof(blank));
    }
    publishSavedLocked();
  }
  give();
  return stored;
}
void publishLink(bool busy,const char *connecting) {
  static uint32_t sampledAt=0;
  static bool previousLink=false;
  const bool connected=WiFi.status()==WL_CONNECTED;
  const bool sample=previousLink!=connected || !sampledAt || millis()-sampledAt>=3000;
  char ssid[33]={}, ip[16]={};
  int8_t rssi=0;
  if (sample) {
    previousLink=connected;
    sampledAt=millis();
    if (connected) {
      WiFi.SSID().toCharArray(ssid,sizeof(ssid));
      WiFi.localIP().toString().toCharArray(ip,sizeof(ip));
      rssi=static_cast<int8_t>(constrain(WiFi.RSSI(),-100,0));
    }
  }
  if (!take()) return;
  busy=busy || wantedWifi!=WifiCommand::None;
  bool changed=state.wifiBusy!=busy || strcmp(state.connectingSsid,connecting)!=0;
  state.wifiBusy=busy;
  strcpy(state.connectingSsid,connecting);
  if (sample) {
    changed=changed || strcmp(state.connectedSsid,ssid)!=0 || strcmp(state.ip,ip)!=0 || state.rssi!=rssi;
    strcpy(state.connectedSsid,ssid);
    strcpy(state.ip,ip);
    state.rssi=rssi;
  }
  if (changed) ++state.wifiGeneration;
  give();
}
void recordRequest(bool warm,int response) {
  if (!take()) return;
  ++state.httpRequests;
  // A successful response on an already-connected TLS socket is evidence of
  // real keep-alive reuse. An apparent warm socket that has gone stale fails
  // and is not counted.
  if (warm && response>=200 && response<300) ++state.warmResponses;
  give();
}
uint64_t tokenFingerprint(const char *token) {
  uint64_t hash=14695981039346656037ull;
  for (const unsigned char *p=reinterpret_cast<const unsigned char*>(token); *p; ++p)
    hash=(hash^*p)*1099511628211ull;
  return hash;
}
bool validToken(const String& token) {
  if (token.length()<16 || token.length()>128) return false;
  for (unsigned i=0;i<token.length();++i) {
    const char c=token[i];
    if (c<33 || c>126) return false;
  }
  return true;
}
void portalRoutes() {
  if (portalRoutesReady) return;
  portal.on("/",HTTP_GET,[](){
    portal.sendHeader("Cache-Control","no-store");
    String page =
      "<!doctype html><html lang='zh'><meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>DeskPet 配网</title><style>body{background:#0d1016;color:#f4eee3;font:16px sans-serif;"
      "max-width:480px;margin:42px auto;padding:20px}input,button{box-sizing:border-box;width:100%;padding:13px;"
      "margin:9px 0;border:1px solid #caa776;border-radius:8px;font:inherit}button{background:#dec08a}"
      "</style><h1>DeskPet 配网</h1><p>填写 2.4 GHz Wi-Fi 和网站设备令牌。令牌可重新输入以轮换，宠物角色设置不会清除。</p>"
      "<form action='/save' method='post'><input type='hidden' name='nonce' value='";
    page += portalNonce;
    page += "'><label>Wi-Fi 名称<input name='ssid' maxlength='32' required></label>"
      "<label>Wi-Fi 密码<input name='password' type='password' maxlength='64'></label>"
      "<label>设备令牌<input name='token' type='password' minlength='16' maxlength='128' required></label>"
      "<button type='submit'>保存并连接</button></form></html>";
    portal.send(200,"text/html; charset=utf-8",page);
  });
  portal.on("/save",HTTP_POST,[](){
    const String ssid=portal.arg("ssid"), password=portal.arg("password"), token=portal.arg("token");
    if (portal.arg("nonce")!=portalNonce || !EspProvisionInput::validSsid(ssid.c_str()) ||
        !EspProvisionInput::validPassword(password.c_str()) || !validToken(token)) {
      portal.send(400,"text/plain; charset=utf-8","输入无效：请检查 Wi-Fi 名称、密码和令牌长度。");
      return;
    }
    if (WifiNetworkStore::find(networks,ssid.c_str())<0 && networks.count>=WifiNetworkStore::Capacity) {
      portal.send(409,"text/plain; charset=utf-8","已保存 5 个网络，请先在板子上忘记一个网络。");
      return;
    }
    ssid.toCharArray(pendingSsid,sizeof(pendingSsid));
    password.toCharArray(pendingPass,sizeof(pendingPass));
    token.toCharArray(pendingToken,sizeof(pendingToken));
    portal.sendHeader("Cache-Control","no-store");
    portal.send(200,"text/html; charset=utf-8",
      "<meta name='viewport' content='width=device-width,initial-scale=1'><h2>正在连接</h2>"
      "<p>连接成功后才会保存 Wi-Fi 和令牌。请在桌宠屏幕上查看结果，可以返回原来的网络。</p>");
    portalSubmitted=true;
  });
  portal.onNotFound([](){ portal.send(404,"text/plain","Not found"); });
  portalRoutesReady=true;
}
void randomPassword(char (&out)[20]) {
  static constexpr char alphabet[]="ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  for (unsigned i=0;i<12;++i) out[i]=alphabet[esp_random()%(sizeof(alphabet)-1)];
  out[12]=0;
}
void randomNonce() {
  static constexpr char hex[]="0123456789abcdef";
  for (unsigned i=0;i<16;++i) portalNonce[i]=hex[esp_random()&15u];
  portalNonce[16]=0;
}
bool startPortal() {
  WiFi.disconnect(true,false);
  WiFi.mode(WIFI_AP);
  char ssid[28], pass[20];
  snprintf(ssid,sizeof(ssid),"DeskPet-%06lX",static_cast<unsigned long>(ESP.getEfuseMac()&0xFFFFFFu));
  randomPassword(pass);
  randomNonce();
  if (!WiFi.softAP(ssid,pass,1,false,1)) {
    WiFi.mode(WIFI_STA);
    error("AP FAILED");
    return false;
  }
  portalRoutes();
  portal.begin();
  portalStarted=millis();
  if (take()) {
    state.portal=true; state.wifi=false; state.clock=false; state.server=false;
    strncpy(state.apSsid,ssid,sizeof(state.apSsid)-1);
    strncpy(state.apPassword,pass,sizeof(state.apPassword)-1);
    state.error[0]=0;
    give();
  }
  return true;
}
void stopPortal() {
  portal.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  if (take()) {
    state.portal=false; state.apSsid[0]=0; state.apPassword[0]=0;
    give();
  }
}
void requestHeaders(HTTPClient& http) {
  // HTTPClient::end() retains the socket only while both reuse flags stay
  // true. Keep this HTTPClient AND its NetworkClientSecure alive on the worker.
  http.setReuse(true);
  http.setConnectTimeout(3500);
  http.setTimeout(5000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  http.addHeader("Authorization",String("Bearer ")+currentToken);
  http.setAcceptEncoding("identity");
}
bool beginRequest(HTTPClient& http,WiFiClientSecure& tls,const String& path) {
  // All requests share one fixed HTTPS host; never carry the Bearer header to
  // a redirect or other authority. Direct begin updates only the path while
  // preserving an already validated same-host socket.
  if (!http.begin(tls,Host,443,path,true)) {
    tls.stop();
    error("TLS SETUP");
    return false;
  }
  requestHeaders(http);
  return true;
}
void endRequest(HTTPClient& http,WiFiClientSecure& tls,bool fullyConsumed) {
  if (!fullyConsumed) http.setReuse(false);
  http.end();
  if (!fullyConsumed) tls.stop();
}
PairStatus pair(HTTPClient& oldHttp,WiFiClientSecure& tls,const char *code,char (&newToken)[129]) {
  // Pairing is one-off and unauthenticated. Close the polling socket before
  // creating a new HTTPClient so its Bearer header cannot accompany the code.
  oldHttp.setReuse(false);
  oldHttp.end();
  tls.stop();
  HTTPClient request;
  if (!request.begin(tls,Host,443,"/api/device/pair",true)) return PairStatus::Offline;
  request.setReuse(false);
  request.setConnectTimeout(3500);
  request.setTimeout(5000);
  request.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  request.setAcceptEncoding("identity");
  request.addHeader("Content-Type","application/json");
  const String body=String("{\"code\":\"")+code+"\"}";
  const int response=request.POST(body);
  recordRequest(false,response);
  PairStatus result=PairStatus::Failed;
  if (response==200) {
    const int length=request.getSize();
    if (length>0 && length<=192) {
      const String payload=request.getString();
      if (payload.length()==static_cast<unsigned>(length) &&
          EspMessageProtocol::parsePairToken(payload.c_str(),newToken)) result=PairStatus::Success;
    }
  } else if (response==400 || response==403) result=PairStatus::Invalid;
  else if (response==429) result=PairStatus::NewCode;
  else if (response<0) result=PairStatus::Offline;
  request.end();
  tls.stop();
  return result;
}
bool latest(HTTPClient& http,WiFiClientSecure& tls,EspMessageProtocol::Latest& result) {
  if (!beginRequest(http,tls,"/api/device/latest")) return false;
  const bool warm=tls.connected();
  const int code=http.GET();
  recordRequest(warm,code);
  bool ok=false;
  if (code==200) {
    const int length=http.getSize();
    if (length>0 && length<=192) {
      const String body=http.getString();
      ok=body.length()==static_cast<unsigned>(length) &&
         EspMessageProtocol::parseLatest(body.c_str(),result);
    }
  }
  endRequest(http,tls,ok);
  if (!ok) {
    const bool rejected=code==401 || code==403;
    error(rejected?"TOKEN REJECTED":"SERVER OFFLINE");
    if (rejected && take()) { state.pair=PairStatus::TokenRejected; give(); }
  }
  return ok;
}
bool download(HTTPClient& http,WiFiClientSecure& tls,const EspMessageProtocol::Latest& metadata) {
  if (!beginRequest(http,tls,String("/api/device/card/")+metadata.revision)) return false;
  const bool warm=tls.connected();
  const int code=http.GET();
  recordRequest(warm,code);
  const int length=http.getSize();
  EspCardTransfer::Result transfer;
  uint32_t actualCrc=0;
  const char *failure=code==200?"CARD LENGTH":"CARD HTTP";
  if (code==200 && length==static_cast<int>(EspMessageProtocol::CardBytes)) {
    NetworkClient *stream=http.getStreamPtr();
    if (stream) {
      transfer=EspCardTransfer::readExact(*stream,back,EspMessageProtocol::CardBytes,
        [](){ return millis(); },[](){ vTaskDelay(pdMS_TO_TICKS(2)?pdMS_TO_TICKS(2):1); });
      if (transfer.outcome==EspCardTransfer::Outcome::Complete) {
        actualCrc=EspMessageProtocol::crc32(back,transfer.bytes);
        failure=actualCrc==metadata.crc32?nullptr:"CARD CRC";
      } else failure="CARD SHORT";
    } else failure="CARD SHORT";
  }
  if (take(100)) {
    state.cardHttpCode=code;
    state.cardContentLength=length;
    state.cardReadBytes=static_cast<uint32_t>(transfer.bytes);
    state.cardExpectedCrc=metadata.crc32;
    state.cardActualCrc=actualCrc;
    state.cardTransferMs=transfer.elapsedMs;
    give();
  }
  const bool ok=failure==nullptr;
  endRequest(http,tls,ok);
  if (!ok) { error(failure); return false; }
  if (take(100)) {
    uint8_t *old=front; front=back; back=old;
    state.hasCard=true;
    state.revision=metadata.revision;
    state.receivedAtMs=millis();
    state.error[0]=0;
    give();
    return true;
  }
  return false;
}
int ack(HTTPClient& http,WiFiClientSecure& tls,uint32_t revision) {
  if (!beginRequest(http,tls,"/api/device/ack")) return -1;
  http.addHeader("Content-Type","application/json");
  const String body=String("{\"revision\":")+revision+'}';
  const bool warm=tls.connected();
  const int code=http.POST(body);
  recordRequest(warm,code);
  const int size=http.getSize();
  // ACK's small response must be consumed before the socket can be reused.
  bool consumed=code>=200 && code<300 && size==0;
  if (code>=200 && code<300 && size>0 && size<=256)
    consumed=http.getString().length()==static_cast<unsigned>(size);
  endRequest(http,tls,consumed);
  return code;
}
bool finishScan(int16_t found) {
  if (!take(100)) return false;
  state.scanCount=0;
  memset(state.scanSsids,0,sizeof(state.scanSsids));
  memset(state.scanSaved,0,sizeof(state.scanSaved));
  for (int i=0;i<found && state.scanCount<12;++i) {
    const String ssid=WiFi.SSID(i);
    if (ssid.isEmpty() || ssid.length()>32) continue;
    const uint8_t auth=static_cast<uint8_t>(WiFi.encryptionType(i));
    bool duplicate=false;
    for (uint8_t j=0;j<state.scanCount;++j)
      if (EspProvisionInput::sameScanEntry(ssid.c_str(),auth,state.scanSsids[j],state.scanAuth[j])) {
        duplicate=true; break;
      }
    if (duplicate) continue;
    const uint8_t index=state.scanCount++;
    ssid.toCharArray(state.scanSsids[index],sizeof(state.scanSsids[index]));
    state.scanRssi[index]=static_cast<int8_t>(constrain(WiFi.RSSI(i),-100,0));
    state.scanAuth[index]=auth;
    state.scanOpen[index]=auth==WIFI_AUTH_OPEN;
    state.scanSaved[index]=WifiNetworkStore::find(networks,ssid.c_str())>=0;
  }
  state.scanning=false;
  ++state.scanGeneration;
  if (found<0) { strcpy(state.error,"SCAN FAILED"); ++state.wifiGeneration; }
  else if (!strcmp(state.error,"SCAN FAILED")) { state.error[0]=0; ++state.wifiGeneration; }
  give();
  WiFi.scanDelete();
  return true;
}
void cancelScan() {
  esp_wifi_scan_stop();
  WiFi.scanDelete();
  if (take(1000)) {
    if (state.scanning) { state.scanning=false; ++state.scanGeneration; }
    give();
  }
}
void worker(void *) {
  WiFiClientSecure tls;
  tls.setCACert(ISRG_ROOT_X1);
  tls.setHandshakeTimeout(5);
  HTTPClient http;
  // The bounded scheduler below owns reconnects; the driver must not keep
  // retrying one stale password behind our back while we rotate saved WLANs.
  WiFi.persistent(false);
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false,false);
  uint32_t lastPoll=0, lastAckTry=0, pendingAck=0;
  uint32_t attemptStarted=0, nextAttemptAt=0, scanStarted=0;
  uint32_t clockWaitStarted=0;
  char pairCode[9]={};
  bool ntpStarted=false, timeSynced=false, joining=false, wasConnected=false, scanRunning=false;
  uint8_t retryIndex=networks.count?networks.preferred:0, attemptsInRound=0;
  WifiNetworkStore::Store staged={};
  bool stageActive=false;
  auto clearStage=[&]() { memset(&staged,0,sizeof(staged)); stageActive=false; };
  auto clearPair=[&]() {
    memset(pairCode,0,sizeof(pairCode));
    if (take()) { state.pair=PairStatus::Idle; give(); }
  };
  auto startAttempt=[&](const char *ssid,const char *password) {
    tls.stop();
    if (scanRunning || state.scanning) { cancelScan(); scanRunning=false; }
    WiFi.disconnect(false,false);
    strcpy(currentSsid,ssid);
    strcpy(currentPass,password);
    joining=true;
    wasConnected=false;
    attemptStarted=millis();
    ntpStarted=false;
    timeSynced=time(nullptr)>=SafeEpoch;
    clockWaitStarted=0;
    lastPoll=0;
    if (take()) {
      state.wifi=state.server=false;
      state.clock=timeSynced;
      state.error[0]=0;
      ++state.wifiGeneration;
      give();
    }
    publishLink(true,currentSsid);
    WiFi.begin(currentSsid,currentPass);
  };
  for (;;) {
    bool start=false, cancel=false, scan=false;
    WifiCommand command=WifiCommand::None;
    char newSsid[33]={}, newPass[65]={};
    if (take()) {
      start=wantPortal; wantPortal=false; cancel=wantCancel; wantCancel=false;
      scan=wantScan; wantScan=false;
      command=wantedWifi; wantedWifi=WifiCommand::None;
      if (command!=WifiCommand::None) {
        strcpy(newSsid,requestedSsid); strcpy(newPass,requestedPass);
        memset(requestedSsid,0,sizeof(requestedSsid)); memset(requestedPass,0,sizeof(requestedPass));
      }
      if (wantPair) { strcpy(pairCode,requestedCode); memset(requestedCode,0,sizeof(requestedCode)); wantPair=false; }
      give();
    }
    if (command!=WifiCommand::None && (!state.portal || command==WifiCommand::Clear)) {
      if (scanRunning || state.scanning) { cancelScan(); scanRunning=false; }
      scan=false;
      if (command==WifiCommand::Clear) {
        start=cancel=scan=false;
        if (state.portal) stopPortal();
        clearStage(); clearPair();
        tls.stop();
        WiFi.disconnect(false,false,500);
        joining=wasConnected=false;
        ntpStarted=false;
        currentSsid[0]=0; memset(currentPass,0,sizeof(currentPass));
        memset(pendingSsid,0,sizeof(pendingSsid));
        memset(pendingPass,0,sizeof(pendingPass));
        memset(pendingToken,0,sizeof(pendingToken));
        portalSubmitted=false;
        WifiNetworkStore::Store replacement=networks;
        WifiNetworkStore::clear(replacement);
        const bool cleared=commitStore(replacement);
        memset(&replacement,0,sizeof(replacement));
        bool legacyCleared=false, driverCleared=false;
        if (cleared) {
          // Keep legacy rollback pairing, but remove its old SSID/password too.
          WifiNetworkStore::Legacy legacy={};
          strcpy(legacy.token,currentToken);
          if (take(1000)) {
            if (currentToken[0]) legacyCleared=saved.putBytes("credentials",&legacy,sizeof(legacy))==sizeof(legacy);
            else legacyCleared=!saved.isKey("credentials") || saved.remove("credentials");
            give();
          }
          memset(&legacy,0,sizeof(legacy));
          // Arduino 3.3.11 STA::disconnect(eraseap) uses exactly this interface
          // config. Switch storage temporarily so old driver Flash credentials,
          // not just the normal RAM-only configuration, are cleared as requested.
          wifi_config_t blank={};
          const esp_err_t flash=esp_wifi_set_storage(WIFI_STORAGE_FLASH);
          const esp_err_t empty=flash==ESP_OK?esp_wifi_set_config(WIFI_IF_STA,&blank):flash;
          const esp_err_t ram=esp_wifi_set_storage(WIFI_STORAGE_RAM);
          driverCleared=flash==ESP_OK && empty==ESP_OK && ram==ESP_OK;
        }
        retryIndex=0; attemptsInRound=0; nextAttemptAt=millis()+RetryPauseMs;
        if (take(1000)) {
          wantScan=wantPortal=wantCancel=wantPair=false;
          memset(requestedSsid,0,sizeof(requestedSsid));
          memset(requestedPass,0,sizeof(requestedPass));
          memset(requestedCode,0,sizeof(requestedCode));
          state.scanning=false; state.scanCount=0;
          memset(state.scanSsids,0,sizeof(state.scanSsids));
          memset(state.scanRssi,0,sizeof(state.scanRssi));
          memset(state.scanOpen,0,sizeof(state.scanOpen));
          memset(state.scanAuth,0,sizeof(state.scanAuth));
          memset(state.scanSaved,0,sizeof(state.scanSaved));
          state.wifi=state.server=false;
          state.wifiBusy=false;
          state.connectedSsid[0]=state.connectingSsid[0]=state.ip[0]=0;
          state.rssi=0;
          state.error[0]=0;
          clearQueued=false;
          ++state.scanGeneration; ++state.wifiGeneration;
          give();
        }
        if (!cleared || !legacyCleared) error("NVS FAILED");
        else if (!driverCleared) error("WLAN CLEAR FAIL");
      } else if (command==WifiCommand::Forget) {
        WifiNetworkStore::Store replacement=networks;
        const auto result=WifiNetworkStore::forget(replacement,newSsid);
        if (result==WifiNetworkStore::Result::NotFound) error("WLAN NOT SAVED");
        else if (!commitStore(replacement)) error("NVS FAILED");
        else {
          // Never allow an earlier staged record to resurrect the just-forgotten SSID.
          const bool stop=stageActive || strcmp(currentSsid,newSsid)==0;
          clearStage();
          if (stop) {
            tls.stop(); WiFi.disconnect(false,false);
            joining=wasConnected=false;
            currentSsid[0]=0; memset(currentPass,0,sizeof(currentPass));
            ntpStarted=false;
            clearPair();
          }
          retryIndex=networks.count?networks.preferred:0;
          attemptsInRound=0; nextAttemptAt=0;
          if (take()) { state.error[0]=0; ++state.wifiGeneration; give(); }
        }
        memset(&replacement,0,sizeof(replacement));
      } else {
        clearStage();
        staged=networks;
        const auto result=command==WifiCommand::Save ?
          WifiNetworkStore::upsert(staged,newSsid,newPass) : WifiNetworkStore::prefer(staged,newSsid);
        if (result==WifiNetworkStore::Result::Full) { clearStage(); error("WLAN LIST FULL"); }
        else if (result==WifiNetworkStore::Result::Invalid || result==WifiNetworkStore::Result::NotFound) {
          clearStage(); error("WLAN NOT SAVED");
        } else {
          stageActive=true;
          clearPair();
          const auto& network=staged.networks[staged.preferred];
          attemptsInRound=0;
          const int existing=WifiNetworkStore::find(networks,newSsid);
          retryIndex=networks.count ? (existing<0?networks.preferred:(existing+1)%networks.count) : 0;
          startAttempt(network.ssid,network.pass);
        }
      }
    }
    memset(newPass,0,sizeof(newPass));
    if (start && !state.portal) {
      tls.stop();
      if (scanRunning || state.scanning) { cancelScan(); scanRunning=false; }
      clearStage(); clearPair(); joining=wasConnected=false;
      startPortal();
      publishLink(false,"");
      lastPoll=0;
      ntpStarted=timeSynced=false;
      clockWaitStarted=0;
    }
    if (state.portal) {
      portal.handleClient();
      if (portalSubmitted) {
        portalSubmitted=false;
        tls.stop();
        staged=networks;
        const auto result=WifiNetworkStore::upsert(staged,pendingSsid,pendingPass);
        strcpy(staged.token,pendingToken);
        stageActive=result==WifiNetworkStore::Result::Changed || result==WifiNetworkStore::Result::Unchanged;
        stopPortal();
        retryIndex=networks.count?networks.preferred:0; attemptsInRound=0;
        if (stageActive) startAttempt(pendingSsid,pendingPass);
        else { clearStage(); error("WLAN LIST FULL"); }
        memset(pendingSsid,0,sizeof(pendingSsid));
        memset(pendingPass,0,sizeof(pendingPass));
        memset(pendingToken,0,sizeof(pendingToken));
      } else if (cancel || millis()-portalStarted>PortalMs) {
        stopPortal();
        retryIndex=networks.count?networks.preferred:0;
        attemptsInRound=0; nextAttemptAt=0;
      } else { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
    }
    if (scan && !state.portal) {
      if (joining) { WiFi.disconnect(false,false); joining=false; clearStage(); }
      WiFi.scanDelete();
      if (take()) { state.scanning=true; state.scanCount=0; give(); }
      const int16_t found=WiFi.scanNetworks(true,true,false,120);
      scanRunning=found==WIFI_SCAN_RUNNING;
      scanStarted=millis();
      if (!scanRunning) scanRunning=!finishScan(found);
    }
    if (scanRunning) {
      const int16_t found=WiFi.scanComplete();
      if (found!=WIFI_SCAN_RUNNING) {
        if (finishScan(found)) scanRunning=false;
      } else if (millis()-scanStarted>ScanMs) {
        cancelScan(); scanRunning=false; error("SCAN FAILED");
      }
    }
    const bool connectedReady=WiFi.status()==WL_CONNECTED &&
      (!joining || (millis()-attemptStarted>=500 && WiFi.SSID()==currentSsid));
    if (!connectedReady) {
      if (tls.connected()) tls.stop();
      status(false,false,false);
      ntpStarted=false;
      // A Wi-Fi interruption does not erase a valid RTC/SNTP system clock.
      timeSynced=time(nullptr)>=SafeEpoch;
      clockWaitStarted=0;
      const uint32_t now=millis();
      if (wasConnected) {
        wasConnected=false;
        attemptsInRound=0;
        const int previous=WifiNetworkStore::find(networks,currentSsid);
        retryIndex=previous>=0?static_cast<uint8_t>(previous):(networks.count?networks.preferred:0);
        nextAttemptAt=now+750;
      }
      const wl_status_t connectStatus=WiFi.status();
      if (joining && (now-attemptStarted>=ConnectMs ||
          (now-attemptStarted>=1500 && (connectStatus==WL_NO_SSID_AVAIL || connectStatus==WL_CONNECT_FAILED)))) {
        WiFi.disconnect(false,false);
        joining=false;
        clearStage(); // Never persist an unverified password or replace a paired token.
        error(connectStatus==WL_NO_SSID_AVAIL?"SSID NOT FOUND":
          (connectStatus==WL_CONNECT_FAILED?"WLAN AUTH FAIL":"WLAN TIMEOUT"));
        ++attemptsInRound;
        if (!networks.count || attemptsInRound>=networks.count) { attemptsInRound=0; nextAttemptAt=now+RetryPauseMs; }
        else nextAttemptAt=now+1000;
      }
      if (!joining && !scanRunning && networks.count && (!nextAttemptAt || static_cast<int32_t>(now-nextAttemptAt)>=0)) {
        if (retryIndex>=networks.count) retryIndex=networks.preferred;
        const auto& network=networks.networks[retryIndex];
        retryIndex=(retryIndex+1)%networks.count;
        startAttempt(network.ssid,network.pass);
      }
      publishLink(joining,joining?currentSsid:"");
      vTaskDelay(pdMS_TO_TICKS(80));
      continue;
    }
    if (joining) {
      if (stageActive) {
        const bool tokenChanged=strcmp(currentToken,staged.token)!=0;
        if (commitStore(staged)) {
          if (tokenChanged) { pendingAck=lastAckTry=0; lastPoll=0; }
        } else error("NVS FAILED");
        clearStage();
      }
      joining=false;
      attemptsInRound=0;
    }
    wasConnected=true;
    publishLink(false,"");
    if (!ntpStarted) {
      esp_sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
      configTime(0,0,"pool.ntp.org","time.google.com");
      ntpStarted=true;
      clockWaitStarted=millis();
    }
    if (!timeSynced && time(nullptr)>=SafeEpoch) timeSynced=true;
    if (!timeSynced || time(nullptr)<SafeEpoch) {
      status(true,false,false);
      error(millis()-clockWaitStarted>=30000 ? "CLOCK SYNC WAIT" : "CLOCK SYNC");
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    status(true,true,state.server);
    if (pairCode[0]) {
      if (take()) { state.pair=PairStatus::Busy; give(); }
      char newToken[129]={};
      PairStatus outcome=pair(http,tls,pairCode,newToken);
      memset(pairCode,0,sizeof(pairCode));
      if (outcome==PairStatus::Success) {
        WifiNetworkStore::Store replacement=networks;
        strcpy(replacement.token,newToken);
        if (commitStore(replacement)) { pendingAck=lastAckTry=0; lastPoll=0; }
        else outcome=PairStatus::SaveFailed;
        memset(&replacement,0,sizeof(replacement));
      }
      memset(newToken,0,sizeof(newToken));
      if (take()) { state.pair=outcome; give(); }
    }
    if (!currentToken[0]) {
      status(true,true,false);
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    const uint32_t now=millis();
    if (!lastPoll || now-lastPoll>=PollMs) {
      lastPoll=now;
      EspMessageProtocol::Latest metadata;
      if (latest(http,tls,metadata)) {
        status(true,true,true);
        uint32_t have=0;
        if (take()) { have=state.revision; state.error[0]=0; give(); }
        if (metadata.revision>0 && metadata.revision!=have && download(http,tls,metadata))
          pendingAck=metadata.revision;
        else if (metadata.revision==0 && !have) pendingAck=0;
      }
    }
    if (pendingAck && (!lastAckTry || now-lastAckTry>=10000)) {
      lastAckTry=now;
      const int result=ack(http,tls,pendingAck);
      if (result==200 || result==204) pendingAck=0;
      else if (result==409) { pendingAck=0; lastPoll=0; }
    }
    vTaskDelay(pdMS_TO_TICKS(60));
  }
}
}

bool begin() {
  guard=xSemaphoreCreateMutex();
  front=static_cast<uint8_t*>(heap_caps_malloc(EspMessageProtocol::CardBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  back=static_cast<uint8_t*>(heap_caps_malloc(EspMessageProtocol::CardBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
  if (!guard || !front || !back) { error("NO MEMORY"); return false; }
  if (!saved.begin("pet-web",false)) { error("NVS FAILED"); return false; }
  WifiNetworkStore::init(networks);
  const size_t storedLength=saved.getBytesLength(StoreKey);
  if (storedLength) {
    WifiNetworkStore::Store loaded={};
    if (storedLength==sizeof(loaded) && saved.getBytes(StoreKey,&loaded,sizeof(loaded))==sizeof(loaded) &&
        WifiNetworkStore::valid(loaded)) {
      networks=loaded; storePersisted=true;
    } else error("NVS INVALID");
    // An existing v2 record is authoritative, including an empty WLAN list.
    // Do not resurrect an explicitly forgotten network from the old key.
    memset(&loaded,0,sizeof(loaded));
  } else if (saved.getBytesLength("credentials")==sizeof(WifiNetworkStore::Legacy)) {
    WifiNetworkStore::Legacy legacy={};
    WifiNetworkStore::Store migrated={};
    if (saved.getBytes("credentials",&legacy,sizeof(legacy))==sizeof(legacy) &&
        WifiNetworkStore::migrate(legacy,migrated)) {
      networks=migrated;
      storePersisted=saved.putBytes(StoreKey,&networks,sizeof(networks))==sizeof(networks);
      if (!storePersisted) error("NVS FAILED");
    } else error("NVS INVALID");
    memset(&legacy,0,sizeof(legacy)); memset(&migrated,0,sizeof(migrated));
  }
  strcpy(currentToken,networks.token);
  DismissState dismissed = {};
  if (saved.getBytesLength("dismiss")==sizeof(dismissed) &&
      saved.getBytes("dismiss",&dismissed,sizeof(dismissed))==sizeof(dismissed) &&
      dismissed.tokenFingerprint==tokenFingerprint(currentToken))
    state.dismissed=dismissed.revision;
  publishSavedLocked(); // Worker has not started; there are no concurrent readers yet.
  state.tokenId=tokenFingerprint(currentToken);
  const bool started=xTaskCreatePinnedToCore(worker,"pet-web",12288,nullptr,1,&workerHandle,0)==pdPASS;
  state.ready=started;
  if (!started) error("TASK FAILED");
  return started;
}
Snapshot snapshot() {
  // Only the UI loop calls snapshot(). Reuse its last good copy if a rare NVS
  // commit holds the mutex, instead of making the UI briefly forget its card.
  static Snapshot lastGood;
  if (take()) { lastGood=state; give(); }
  return lastGood;
}
void requestProvision() {
  if (take()) {
    if (!clearQueued && wantedWifi==WifiCommand::None && state.pair!=PairStatus::Busy) wantPortal=true;
    give();
  }
}
void cancelProvision() { if (take()) { wantCancel=true; give(); } }
bool requestScan() {
  if (!take()) return false;
  if (clearQueued || state.portal || wantPortal || state.scanning || wantScan || wantedWifi!=WifiCommand::None) { give(); return false; }
  wantScan=true; state.scanning=true; ++state.scanGeneration;
  give();
  return true;
}
bool queueWifi(WifiCommand command,const char *ssid,const char *password) {
  if (!EspProvisionInput::validSsid(ssid) || (command==WifiCommand::Save && !EspProvisionInput::validPassword(password))) return false;
  if (!take(100)) return false;
  if (clearQueued || wantedWifi!=WifiCommand::None || state.portal || wantPortal || state.pair==PairStatus::Busy) { give(); return false; }
  const bool exists=WifiNetworkStore::find(networks,ssid)>=0;
  if (command==WifiCommand::Save && !exists && networks.count>=WifiNetworkStore::Capacity) {
    strcpy(state.error,"WLAN LIST FULL"); ++state.wifiGeneration; give(); return false;
  }
  if (command!=WifiCommand::Save && !exists) {
    strcpy(state.error,"WLAN NOT SAVED"); ++state.wifiGeneration; give(); return false;
  }
  strcpy(requestedSsid,ssid);
  memset(requestedPass,0,sizeof(requestedPass));
  if (password) strcpy(requestedPass,password);
  wantedWifi=command;
  state.wifiBusy=true; ++state.wifiGeneration;
  wantScan=false; // A copied explicit selection supersedes a queued scan.
  give();
  return true;
}
bool saveWifi(const char *ssid,const char *password) { return queueWifi(WifiCommand::Save,ssid,password); }
bool connectSaved(const char *ssid) { return queueWifi(WifiCommand::Connect,ssid,nullptr); }
bool forgetWifi(const char *ssid) { return queueWifi(WifiCommand::Forget,ssid,nullptr); }
bool clearWifi() {
  if (!take(100)) return false;
  if (clearQueued || wantedWifi!=WifiCommand::None || state.pair==PairStatus::Busy) { give(); return false; }
  clearQueued=true;
  wantedWifi=WifiCommand::Clear;
  wantScan=wantPortal=wantCancel=wantPair=false;
  memset(requestedSsid,0,sizeof(requestedSsid));
  memset(requestedPass,0,sizeof(requestedPass));
  memset(requestedCode,0,sizeof(requestedCode));
  state.wifiBusy=true; ++state.wifiGeneration;
  give();
  return true;
}
bool requestPair(const char *code) {
  if (!EspProvisionInput::validCode(code) || !take(100)) return false;
  if (clearQueued || wantPair || state.portal || !state.wifiSaved ||
      state.pair==PairStatus::Busy || state.pair==PairStatus::Waiting) { give(); return false; }
  strcpy(requestedCode,code);
  state.pair=PairStatus::Waiting;
  wantPair=true;
  give();
  return true;
}
void dismiss(uint32_t revision) {
  if (!revision || !take(100)) return;
  if (revision>state.dismissed) {
    state.dismissed=revision;
    const DismissState record={tokenFingerprint(currentToken),revision};
    saved.putBytes("dismiss",&record,sizeof(record));
  }
  give();
}
bool drawCard(Arduino_Canvas& canvas, uint32_t& revision) {
  if (!take(100)) return false;
  if (!state.hasCard || !front) { give(); return false; }
  uint16_t *pixels=canvas.getFramebuffer();
  if (!pixels) { give(); return false; }
  for (size_t i=0;i<EspMessageProtocol::CardBytes;++i) {
    const uint8_t value=front[i];
    const uint8_t left=(value>>4)*17u, right=(value&15u)*17u;
    pixels[2*i]=((left>>3)<<11)|((left>>2)<<5)|(left>>3);
    pixels[2*i+1]=((right>>3)<<11)|((right>>2)<<5)|(right>>3);
  }
  revision=state.revision;
  give();
  return true;
}
}
