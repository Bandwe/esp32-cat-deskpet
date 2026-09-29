#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../sketch/DeskPetV01/EspMessagePage.h"

namespace Calls {
int scans=0,connects=0,forgets=0,saves=0,portals=0,cancels=0;
bool accepted=true;
std::string ssid,password;
void reset() { scans=connects=forgets=saves=portals=cancels=0; accepted=true; ssid.clear(); password.clear(); }
}
namespace EspMessageService {
bool requestScan() { ++Calls::scans; return Calls::accepted; }
void requestProvision() { ++Calls::portals; }
void cancelProvision() { ++Calls::cancels; }
bool saveWifi(const char* ssid,const char* pass) { ++Calls::saves; Calls::ssid=ssid; Calls::password=pass; return Calls::accepted; }
bool connectSaved(const char* ssid) { ++Calls::connects; Calls::ssid=ssid; return Calls::accepted; }
bool forgetWifi(const char* ssid) { ++Calls::forgets; Calls::ssid=ssid; return Calls::accepted; }
bool requestPair(const char*) { return Calls::accepted; }
}
using namespace EspMessagePage;
static bool hasText(const Arduino_Canvas& c,const std::string& needle) {
  for (const auto& label:c.labels) if (label.find(needle)!=std::string::npos) return true;
  return false;
}
static EspMessageService::Snapshot fixture() {
  EspMessageService::Snapshot s;
  s.savedCount=2; s.wifiSaved=true; s.wifi=true; s.wifiGeneration=4;
  std::strcpy(s.savedSsids[0],"Home_2G"); std::strcpy(s.savedSsids[1],"Studio_2G");
  std::strcpy(s.connectedSsid,"Home_2G"); std::strcpy(s.ip,"192.168.1.42"); s.rssi=-53;
  s.scanCount=3; s.scanGeneration=8;
  std::strcpy(s.scanSsids[0],"Home_2G"); s.scanSaved[0]=true; s.scanRssi[0]=-53;
  std::strcpy(s.scanSsids[1],"Cafe_2G"); s.scanRssi[1]=-69;
  std::strcpy(s.scanSsids[2],"Guest_Open"); s.scanRssi[2]=-74; s.scanOpen[2]=true;
  return s;
}
int main(int argc,char** argv) {
  Arduino_Canvas canvas;
  Ui ui;
  auto s=fixture();
  Calls::reset();
  assert(tap(ui,50,160,s)==Action::None && ui.view==View::Wifi && Calls::scans==0);
  assert(tap(ui,12,12,s)==Action::None && ui.view==View::Home);
  ui.view=View::Wifi; ui.directWifi=true;
  assert(tap(ui,12,12,s)==Action::Back);
  hint(ui,"Connection requested"); std::strcpy(s.error,"WLAN AUTH FAIL"); draw(canvas,s,ui);
  assert(hasText(canvas,"WLAN AUTH FAIL") && !hasText(canvas,"Connection requested"));
  s.error[0]=0;
  ui.directWifi=false;
  tap(ui,80,190,s); assert(ui.view==View::Saved);
  // A tap before rendering, or after a background list update, cannot select a row.
  tap(ui,30,75,s); assert(ui.view==View::Saved);
  draw(canvas,s,ui); ++s.wifiGeneration;
  tap(ui,30,75,s); assert(ui.view==View::Saved);
  draw(canvas,s,ui); tap(ui,30,75,s);
  assert(ui.view==View::Network && !std::strcmp(ui.ssid,"Home_2G"));
  std::strcpy(s.savedSsids[0],"Other"); std::strcpy(s.savedSsids[1],"Home_2G");
  tap(ui,70,137,s);
  assert(Calls::connects==1 && Calls::ssid=="Home_2G" && ui.view==View::Wifi);
  // Password edits start blank and never show stored secrets.
  ui.view=View::Network; std::strcpy(ui.password,"OldPassword");
  tap(ui,70,183,s); assert(ui.view==View::Password && !ui.password[0] && !ui.reveal);
  std::strcpy(ui.password,"NewPassword"); draw(canvas,s,ui);
  assert(!hasText(canvas,"NewPassword") && hasText(canvas,"***********"));
  tap(ui,210,15,s); draw(canvas,s,ui); assert(hasText(canvas,"NewPassword"));
  tap(ui,10,15,s); assert(ui.view==View::Network && !ui.password[0] && !ui.reveal);
  tap(ui,70,239,s); assert(ui.view==View::Forget);
  tap(ui,70,195,s); assert(ui.view==View::Network && Calls::forgets==0);
  tap(ui,70,239,s); tap(ui,70,249,s);
  assert(ui.view==View::Saved && Calls::forgets==1 && Calls::ssid=="Home_2G");
  // Scan waits for a completed new generation, not the previous visible scan.
  ui.view=View::Wifi; tap(ui,90,143,s);
  assert(ui.view==View::Scan && Calls::scans==1);
  draw(canvas,s,ui); tap(ui,30,70,s); assert(ui.view==View::Scan);
  ++s.scanGeneration; s.scanning=true; draw(canvas,s,ui);
  tap(ui,30,70,s); assert(ui.view==View::Scan);
  s.scanning=false; draw(canvas,s,ui); ++s.scanGeneration;
  tap(ui,30,70,s); assert(ui.view==View::Scan);
  draw(canvas,s,ui); tap(ui,30,70,s);
  assert(ui.view==View::Network && !std::strcmp(ui.ssid,"Home_2G") && Calls::saves==0);
  // Unknown secure network requires a password; open network can join immediately.
  tap(ui,10,10,s); draw(canvas,s,ui); tap(ui,30,110,s);
  assert(ui.view==View::Password && !std::strcmp(ui.ssid,"Cafe_2G"));
  tap(ui,210,252,s); assert(ui.view==View::Password && Calls::saves==0);
  std::strcpy(ui.password,"ValidPass8"); tap(ui,210,252,s);
  assert(ui.view==View::Wifi && Calls::saves==1 && !ui.password[0]);
  ui.view=View::Scan; draw(canvas,s,ui); tap(ui,30,150,s);
  assert(ui.view==View::Wifi && Calls::saves==2 && Calls::password.empty());
  // A full list never silently replaces another network, but editing one is allowed.
  s.savedCount=5; ui.view=View::Scan; draw(canvas,s,ui); tap(ui,30,110,s);
  assert(ui.view==View::Scan && Calls::saves==2 && std::strstr(ui.hint,"full"));
  ui.view=View::Password; std::strcpy(ui.ssid,"Home_2G"); std::strcpy(ui.password,"Update123");
  tap(ui,210,252,s); assert(ui.view==View::Wifi && Calls::saves==3);
  // AP is optional and returns to its source; website routes remain available.
  tap(ui,80,237,s); assert(ui.view==View::Portal && Calls::portals==1);
  tap(ui,80,252,s); assert(ui.view==View::Wifi && Calls::cancels==1);
  ui.view=View::Home; s.hasCard=true;
  assert(tap(ui,40,120,s)==Action::Latest);
  tap(ui,40,210,s); assert(ui.view==View::Pair);
  tap(ui,10,10,s); assert(ui.view==View::Home);
  assert(tap(ui,-1,160,s)==Action::None && ui.view==View::Home);
  Calls::accepted=false; ui.view=View::Wifi; tap(ui,90,143,s); draw(canvas,s,ui);
  assert(ui.view==View::Scan && !ui.awaitScan && hasText(canvas,"Scan busy. Try again."));
  Calls::accepted=true;
  // Familiar nine-key layout: 2 cycles ABC, timeout commits, 0 and punctuation
  // are entered explicitly; hidden passwords stay hidden while composing.
  startEntry(ui,View::Scan); std::strcpy(ui.ssid,"Phone_Key_Test");
  espTestMillis=100; tap(ui,120,90,s); assert(!std::strcmp(ui.password,"a"));
  espTestMillis=200; tap(ui,120,90,s); assert(!std::strcmp(ui.password,"b"));
  assert(!tick(ui,949)); assert(tick(ui,950)); assert(!tick(ui,951));
  espTestMillis=1000; tap(ui,120,90,s); assert(!std::strcmp(ui.password,"ba"));
  draw(canvas,s,ui); assert(!hasText(canvas,"ba 2/64") && hasText(canvas,"** 2/64"));
  tap(ui,90,250,s); assert(ui.keyboard.pendingKey==-1 && ui.keyboard.mode==EspNineKeyInput::Mode::Digits);
  tap(ui,120,201,s); assert(!std::strcmp(ui.password,"ba0"));
  tap(ui,145,250,s); tap(ui,40,90,s); assert(!std::strcmp(ui.password,"ba0!"));
  tap(ui,200,200,s); assert(!std::strcmp(ui.password,"ba0"));
  tap(ui,30,250,s); tap(ui,40,200,s); tap(ui,120,90,s);
  assert(!std::strcmp(ui.password,"ba0A"));
  tap(ui,10,10,s); assert(!ui.password[0] && ui.keyboard.pendingKey==-1 && ui.view==View::Scan);
  reset(ui); assert(ui.keyboard.mode==EspNineKeyInput::Mode::Letters && !ui.keyboard.upper);
  // Optional four previews use only fake SSIDs and the actual draw functions.
  if (argc>1) {
    const std::string dir=argv[1];
    s=fixture(); ui=Ui{}; ui.view=View::Wifi; draw(canvas,s,ui); canvas.saveSvg(dir+"/wlan_dashboard.svg");
    ui.view=View::Scan; draw(canvas,s,ui); canvas.saveSvg(dir+"/wlan_scan.svg");
    ui.view=View::Network; std::strcpy(ui.ssid,"Home_2G"); draw(canvas,s,ui); canvas.saveSvg(dir+"/wlan_network.svg");
    ui.view=View::Password; std::strcpy(ui.password,"example8"); draw(canvas,s,ui); canvas.saveSvg(dir+"/wlan_keyboard.svg");
  }
  std::puts("PASS: R20 WLAN routes, stale-list protection, saved SSID identity, secrets, capacity, confirmations and website routes");
}
