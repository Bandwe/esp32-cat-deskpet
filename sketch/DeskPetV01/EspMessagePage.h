#pragma once
#include <Arduino_GFX_Library.h>
#include "EspMessageService.h"
#include "EspNineKeyInput.h"

namespace EspMessagePage {
enum class Action : uint8_t { None, Latest, Back };
enum class View : uint8_t { Home, Wifi, Saved, Network, Forget, Scan, Ssid, Password, Pair, Portal };
struct Ui {
  View view=View::Home;
  View selectionReturn=View::Saved, entryReturn=View::Scan, portalReturn=View::Wifi;
  uint8_t scanPage=0;
  bool securedNetwork=false, reveal=false, directWifi=false;
  // Only accept rows from the scan frame actually drawn, never a replacement list.
  mutable uint32_t renderedScanGeneration=0;
  mutable uint32_t requestedScanGeneration=0, renderedSavedGeneration=0;
  mutable uint8_t renderedScanPage=0;
  mutable bool renderedScanReady=false, renderedSavedReady=false, awaitScan=false;
  EspNineKeyInput::State keyboard;
  char ssid[33]={}, password[65]={}, code[9]={};
  char hint[28]={};
};
inline void clearSecret(Ui& ui) {
  memset(ui.password,0,sizeof(ui.password));
  memset(ui.code,0,sizeof(ui.code));
  EspNineKeyInput::commit(ui.keyboard);
}
inline void reset(Ui& ui) { clearSecret(ui); ui=Ui{}; }
inline bool tick(Ui& ui,uint32_t now) {
  return (ui.view==View::Ssid || ui.view==View::Password) && EspNineKeyInput::tick(ui.keyboard,now);
}
inline void hint(Ui& ui,const char *message) {
  strncpy(ui.hint,message,sizeof(ui.hint)-1);
  ui.hint[sizeof(ui.hint)-1]=0;
}
inline void text(Arduino_Canvas& c,int x,int y,const char* value,
                 uint16_t color=0xffff,uint8_t size=1) {
  c.setTextColor(color); c.setTextSize(size); c.setCursor(x,y); c.print(value);
}
inline void box(Arduino_Canvas& c,int x,int y,int w,int h,const char* title,bool active=true) {
  const uint16_t edge=active?0xfecf:0x4208;
  c.fillRoundRect(x,y,w,h,6,active?0x1082:0x0841);
  c.drawRoundRect(x,y,w,h,6,edge);
  const int textWidth=strlen(title)*12;
  text(c,x+(w-textWidth)/2,y+(h-16)/2,title,active?0xffff:0x8410,2);
}
inline void smallBox(Arduino_Canvas& c,int x,int y,int w,int h,const char* title) {
  c.fillRoundRect(x,y,w,h,5,0x1082);
  c.drawRoundRect(x,y,w,h,5,0xfecf);
  text(c,x+(w-int(strlen(title))*6)/2,y+(h-8)/2,title);
}
inline void header(Arduino_Canvas& c,const char* title) {
  c.fillScreen(0);
  text(c,7,10,"< BACK",0xfecf);
  text(c,75,8,title,0xffff,2);
  c.drawLine(7,34,233,34,0x4208);
}
inline void printable(const char* source,char *out,size_t capacity,size_t maxChars) {
  size_t n=0;
  for (const unsigned char* p=reinterpret_cast<const unsigned char*>(source);
       *p && n+1<capacity && n<maxChars; ++p)
    out[n++]=(*p>=32 && *p<=126)?static_cast<char>(*p):'?';
  out[n]=0;
}
inline bool saved(const EspMessageService::Snapshot& s,const char* ssid) {
  for (uint8_t i=0;i<s.savedCount && i<5;++i)
    if (!strcmp(s.savedSsids[i],ssid)) return true;
  return false;
}
inline void showWifi(Ui& ui) { clearSecret(ui); ui.reveal=false; ui.view=View::Wifi; }
inline void startScan(Ui& ui,const EspMessageService::Snapshot& s) {
  ui.view=View::Scan; ui.scanPage=0; ui.hint[0]=0;
  ui.renderedScanReady=false;
  ui.requestedScanGeneration=s.scanGeneration;
  ui.awaitScan=EspMessageService::requestScan();
  if (!ui.awaitScan) hint(ui,"Scan busy. Try again.");
}
inline void startEntry(Ui& ui,View returnTo) {
  clearSecret(ui); ui.entryReturn=returnTo;
  ui.keyboard=EspNineKeyInput::State{};
  ui.reveal=false; ui.hint[0]=0; ui.view=View::Password;
}
inline void drawWifi(Arduino_Canvas& c,const EspMessageService::Snapshot& s,const Ui& ui) {
  header(c,"WLAN");
  c.fillRoundRect(8,43,224,77,8,0x1082);
  const char* status=s.wifi?"CONNECTED":(s.wifiBusy?"CONNECTING":(s.wifiSaved?"OFFLINE":"NOT SET"));
  c.fillCircle(22,57,4,s.wifi?0x07e0:(s.wifiBusy?0xfecf:0x8410));
  text(c,35,53,status,0xffff);
  char name[33];
  printable(s.wifi?s.connectedSsid:s.connectingSsid,name,sizeof(name),32);
  text(c,16,74,name[0]?name:(s.savedCount?"Choose a saved network":"Add a 2.4 GHz network"),0xfecf);
  char details[36];
  if (s.wifi) snprintf(details,sizeof(details),"%s  %d dBm",s.ip,s.rssi);
  else snprintf(details,sizeof(details),"%u / 5 networks saved",s.savedCount);
  text(c,16,96,details,0x9cd3);
  box(c,8,128,224,39,"AVAILABLE NETWORKS");
  box(c,8,175,224,39,"SAVED NETWORKS");
  smallBox(c,8,222,224,35,"AP SETUP (OPTIONAL)");
  // A queued-action hint must never hide the eventual connection error/result.
  const char* footer=s.error[0]?s.error:(s.wifi?"Saved networks auto reconnect":
      (ui.hint[0]?ui.hint:"2.4 GHz WLAN only"));
  text(c,7,268,footer,s.error[0]?0xfecf:0x9cd3);
}
inline void drawSaved(Arduino_Canvas& c,const EspMessageService::Snapshot& s,const Ui& ui) {
  header(c,"SAVED");
  ui.renderedSavedGeneration=s.wifiGeneration;
  ui.renderedSavedReady=true;
  text(c,8,42,"Tap a network to manage",0x9cd3);
  if (!s.savedCount) {
    text(c,20,94,"No networks saved yet",0xfecf);
    smallBox(c,12,132,216,43,"FIND A NETWORK");
  }
  for (uint8_t i=0;i<s.savedCount && i<5;++i) {
    const int y=61+i*36;
    c.fillRoundRect(7,y,226,33,6,0x1082);
    c.drawRoundRect(7,y,226,33,6,0x4208);
    char name[31]; printable(s.savedSsids[i],name,sizeof(name),29);
    text(c,14,y+5,name);
    const bool connected=s.wifi && !strcmp(s.connectedSsid,s.savedSsids[i]);
    const bool connecting=s.wifiBusy && !strcmp(s.connectingSsid,s.savedSsids[i]);
    text(c,14,y+19,connected?"CONNECTED":(connecting?"CONNECTING":"SAVED"),connected?0x07e0:0x9cd3);
    text(c,217,y+12,">",0xfecf);
  }
  text(c,7,254,ui.hint[0]?ui.hint:"Up to 5 saved networks",0x9cd3);
}
inline void drawNetwork(Arduino_Canvas& c,const EspMessageService::Snapshot& s,const Ui& ui) {
  header(c,"NETWORK");
  char name[33]; printable(ui.ssid,name,sizeof(name),32);
  text(c,8,45,name,0xfecf);
  const bool exists=saved(s,ui.ssid);
  const bool connected=s.wifi && !strcmp(s.connectedSsid,ui.ssid);
  text(c,8,67,connected?"Currently connected":(exists?"Saved network":"Network no longer saved"),0x9cd3);
  text(c,8,87,ui.hint[0]?ui.hint:"Password stays hidden",ui.hint[0]?0xfecf:0x8410);
  box(c,12,116,216,42,"CONNECT",exists && !s.wifiBusy);
  box(c,12,167,216,42,"EDIT PASSWORD",exists && !s.wifiBusy);
  box(c,12,218,216,42,"FORGET",exists && !s.wifiBusy);
}
inline void drawForget(Arduino_Canvas& c,const Ui& ui) {
  header(c,"FORGET");
  char name[33]; printable(ui.ssid,name,sizeof(name),32);
  text(c,8,53,"Remove this saved network?",0xffff);
  text(c,8,79,name,0xfecf);
  text(c,8,110,"Its password will be removed.",0x9cd3);
  text(c,8,128,"Connect again by typing it.",0x9cd3);
  text(c,8,151,ui.hint,0xfecf);
  box(c,12,179,216,40,"KEEP NETWORK");
  box(c,12,230,216,40,"FORGET NOW");
}
inline void drawHome(Arduino_Canvas& c,const EspMessageService::Snapshot& s) {
  c.fillScreen(0);
  text(c,14,13,"WEB NOTE",0xfecf,2);
  c.drawLine(14,37,226,37,0x4208);
  c.fillRoundRect(12,44,216,52,8,0x1082);
  c.fillCircle(28,61,4,s.server?0x07e0:(s.wifi?0xfecf:0x8410));
  text(c,41,53,s.server?"ONLINE":(s.wifi?"WLAN CONNECTED":(s.wifiSaved?"WLAN CONNECTING":"WLAN NOT SET")),0xffff);
  text(c,20,78,s.error[0]?s.error:(s.configured?"Website paired":"Website not paired"),0x9cd3);
  box(c,12,104,216,39,"LAST NOTE",s.hasCard);
  box(c,12,150,216,39,"WLAN NETWORK");
  box(c,12,196,216,39,"PAIR WEBSITE",s.wifiSaved);
  smallBox(c,12,243,216,34,"BACK");
}
inline void drawScan(Arduino_Canvas& c,const EspMessageService::Snapshot& s,const Ui& ui) {
  header(c,"WLAN");
  ui.renderedScanGeneration=s.scanGeneration;
  ui.renderedScanPage=ui.scanPage;
  if (s.scanGeneration!=ui.requestedScanGeneration) ui.awaitScan=false;
  ui.renderedScanReady=!s.scanning && !ui.awaitScan;
  if (!ui.renderedScanReady) text(c,8,39,"Scanning 2.4 GHz...",0x9cd3);
  else if (ui.hint[0]) text(c,8,39,ui.hint,0xfecf);
  else if (!s.scanCount) text(c,8,39,"No SSID found. Scan again.",0x9cd3);
  if (ui.renderedScanReady && s.scanCount) {
    char page[20];
    const unsigned end=s.scanCount<(ui.scanPage+1)*4?s.scanCount:(ui.scanPage+1)*4;
    snprintf(page,sizeof(page),"Networks %u-%u / %u",ui.scanPage*4+1,end,s.scanCount);
    if (!ui.hint[0]) text(c,7,39,page,0x9cd3);
    if (s.scanCount>4) text(c,191,8,"NEXT",0xfecf);
    for (uint8_t row=0;row<4;++row) {
      const uint8_t index=ui.scanPage*4+row;
      if (index>=s.scanCount) break;
      const int y=57+row*42;
      c.fillRoundRect(7,y,226,39,6,0x1082);
      c.drawRoundRect(7,y,226,39,6,0x4208);
      char name[22],label[27];
      printable(s.scanSsids[index],name,sizeof(name),19);
      snprintf(label,sizeof(label),"%02u %s",index+1,name);
      text(c,14,y+6,label,0xffff);
      char details[30];
      snprintf(details,sizeof(details),"%s  %d dBm%s",s.scanOpen[index]?"OPEN":"LOCK",s.scanRssi[index],s.scanSaved[index]?"  SAVED":"");
      text(c,14,y+21,details,0x9cd3);
      text(c,213,y+11,">",0xfecf);
    }
  }
  smallBox(c,5,239,73,37,"SCAN");
  smallBox(c,83,239,73,37,"MANUAL");
  smallBox(c,161,239,74,37,"AP SETUP");
}
inline void drawEntry(Arduino_Canvas& c,const Ui& ui) {
  const bool pass=ui.view==View::Password;
  header(c,pass?"PASSWORD":"SSID");
  if (pass) text(c,190,11,ui.reveal?"HIDE":"SHOW",0xfecf);
  char name[30]; printable(ui.ssid,name,sizeof(name),25);
  if (pass) {
    text(c,7,39,name,0x9cd3);
    const size_t n=strlen(ui.password);
    char masked[24];
    const size_t stars=n<18?n:18;
    if (ui.reveal) memcpy(masked,ui.password+n-stars,stars);
    else memset(masked,'*',stars);
    masked[stars]=0;
    char value[30]; snprintf(value,sizeof(value),"%s %u/64",masked,static_cast<unsigned>(n));
    text(c,7,55,ui.hint[0]?ui.hint:value,ui.hint[0]?0xf800:0xffff);
  } else {
    text(c,7,40,ui.hint[0]?ui.hint:(name[0]?name:"Enter network name"),ui.hint[0]?0xf800:0xffff);
    text(c,7,56,"Hidden SSID: type exact name",0x9cd3);
  }
  using K=EspNineKeyInput::Mode;
  for (uint8_t key=1;key<=9;++key) {
    const int x=((key-1)%3)*80, y=70+((key-1)/3)*38;
    const bool active=ui.keyboard.pendingKey==key;
    const char symbol=EspNineKeyInput::symbol(ui.keyboard.symbolPage,key);
    const bool enabled=ui.keyboard.mode!=K::Symbols || symbol;
    c.fillRoundRect(x+2,y+1,76,36,5,active?0x2945:(enabled?0x1082:0x0841));
    c.drawRoundRect(x+2,y+1,76,36,5,active?0xfecf:0x4208);
    if (ui.keyboard.mode==K::Letters) {
      char digit[2]={char('0'+key),0}; text(c,x+35,y+4,digit,0x9cd3);
      const char* raw=EspNineKeyInput::group(key);
      char letters[6]={};
      for (size_t i=0;raw[i];++i) letters[i]=EspNineKeyInput::applyCase(raw[i],ui.keyboard.upper);
      const int left=x+(80-int(strlen(letters))*12)/2;
      text(c,left,y+16,letters,0xffff,2);
      if (active) {
        const int selected=left+ui.keyboard.choice*12;
        c.drawLine(selected,y+33,selected+9,y+33,0xfecf);
      }
    } else if (enabled) {
      char label[2]={ui.keyboard.mode==K::Digits?char('0'+key):symbol,0};
      text(c,x+34,y+10,label,0xffff,2);
    }
  }
  smallBox(c,2,185,76,36,ui.keyboard.upper?"CAPS ON":"CAPS");
  smallBox(c,82,185,76,36,ui.keyboard.mode==K::Digits?"0":"SPACE");
  smallBox(c,162,185,76,36,"DEL");
  char symbolLabel[9];
  snprintf(symbolLabel,sizeof(symbolLabel),"SYM %u/4",ui.keyboard.symbolPage+1);
  smallBox(c,2,234,57,42,ui.keyboard.mode==K::Letters?"[ABC]":"ABC");
  smallBox(c,61,234,57,42,ui.keyboard.mode==K::Digits?"[123]":"123");
  smallBox(c,120,234,57,42,ui.keyboard.mode==K::Symbols?symbolLabel:"#+=");
  smallBox(c,179,234,59,42,pass?"JOIN":"NEXT");
}
inline const char* pairLabel(EspMessageService::PairStatus status) {
  using P=EspMessageService::PairStatus;
  switch (status) {
    case P::Waiting: return "Waiting for Wi-Fi / clock";
    case P::Busy: return "Pairing...";
    case P::Success: return "Paired. Notes now active.";
    case P::Invalid: return "Invalid or expired code";
    case P::NewCode: return "Get a NEW website code";
    case P::Offline: return "Network unavailable. Retry";
    case P::Failed: return "Server error. Retry";
    case P::SaveFailed: return "Could not save. Retry";
    case P::TokenRejected: return "Token rejected. Pair again";
    default: return "Enter website 8-digit code";
  }
}
inline void drawPair(Arduino_Canvas& c,const EspMessageService::Snapshot& s,const Ui& ui) {
  header(c,"PAIR");
  text(c,7,41,!s.wifi?"Connect WLAN first":pairLabel(s.pair),s.wifi?0x9cd3:0xfecf);
  char shown[12]="____ ____";
  const size_t n=strlen(ui.code);
  for (size_t i=0;i<n && i<8;++i) shown[i+(i>=4)]=ui.code[i];
  text(c,51,60,shown,0xffff,2);
  static constexpr char keys[12]={'1','2','3','4','5','6','7','8','9','<','0','G'};
  for (uint8_t row=0;row<4;++row) for (uint8_t col=0;col<3;++col) {
    const int x=9+col*75,y=84+row*39;
    c.fillRoundRect(x,y,71,37,5,0x1082);
    c.drawRoundRect(x,y,71,37,5,0xfecf);
    const char key=keys[row*3+col];
    if (key=='G') text(c,x+21,y+11,"GO",0xffff,2);
    else { char label[2]={key,0}; text(c,x+29,y+11,label,0xffff,2); }
  }
  text(c,16,249,"Code lasts 10 min on website",0x8410);
}
inline void drawPortal(Arduino_Canvas& c,const EspMessageService::Snapshot& s) {
  header(c,"AP SETUP");
  if (!s.portal) text(c,14,51,s.error[0]?s.error:"Starting backup AP...",0xfecf);
  else {
    text(c,14,48,"1. Connect phone to:",0xbdf7);
    text(c,14,68,s.apSsid,0xffff,2);
    text(c,14,106,"Password",0xbdf7);
    text(c,14,124,s.apPassword,0xffff,2);
    text(c,14,163,"2. Open in browser:",0xbdf7);
    text(c,14,182,"192.168.4.1",0xffff,2);
    text(c,14,214,"Closes after 5 min",0x8410);
  }
  smallBox(c,12,238,216,38,"BACK");
}
inline void draw(Arduino_Canvas& c,const EspMessageService::Snapshot& s,const Ui& ui) {
  switch (ui.view) {
    case View::Home: drawHome(c,s); break;
    case View::Wifi: drawWifi(c,s,ui); break;
    case View::Saved: drawSaved(c,s,ui); break;
    case View::Network: drawNetwork(c,s,ui); break;
    case View::Forget: drawForget(c,ui); break;
    case View::Scan: drawScan(c,s,ui); break;
    case View::Ssid: case View::Password: drawEntry(c,ui); break;
    case View::Pair: drawPair(c,s,ui); break;
    case View::Portal: drawPortal(c,s); break;
  }
}
inline Action tap(Ui& ui,int x,int y,const EspMessageService::Snapshot& s) {
  if (x<0 || x>=240 || y<0 || y>=280) return Action::None;
  if (ui.view==View::Home) {
    if (x<12 || x>228) return Action::None;
    if (y>=104 && y<143 && s.hasCard) return Action::Latest;
    if (y>=150 && y<189) {
      ui.directWifi=false; ui.hint[0]=0; showWifi(ui); return Action::None;
    }
    if (y>=196 && y<235 && s.wifiSaved) { ui.view=View::Pair; ui.code[0]=0; return Action::None; }
    if (y>=243) return Action::Back;
    return Action::None;
  }
  if (ui.view==View::Portal) {
    if ((x<68 && y<35) || y>=238) {
      EspMessageService::cancelProvision(); ui.view=ui.portalReturn;
    }
    return Action::None;
  }
  if (x<68 && y<35) {
    ui.hint[0]=0;
    if (ui.view==View::Wifi) {
      clearSecret(ui);
      if (ui.directWifi) return Action::Back;
      ui.view=View::Home;
    } else if (ui.view==View::Pair) { clearSecret(ui); ui.view=View::Home; }
    else if (ui.view==View::Scan || ui.view==View::Saved) showWifi(ui);
    else if (ui.view==View::Network) ui.view=ui.selectionReturn;
    else if (ui.view==View::Forget) ui.view=View::Network;
    else if (ui.view==View::Password || ui.view==View::Ssid) {
      clearSecret(ui); ui.reveal=false; ui.view=ui.entryReturn;
    }
    return Action::None;
  }
  if (ui.view==View::Wifi) {
    if (x<8 || x>=232) return Action::None;
    if (y>=128 && y<167) startScan(ui,s);
    else if (y>=175 && y<214) {
      ui.hint[0]=0; ui.renderedSavedReady=false; ui.view=View::Saved;
    } else if (y>=222 && y<257) {
      ui.portalReturn=View::Wifi; ui.view=View::Portal;
      EspMessageService::requestProvision();
    }
    return Action::None;
  }
  if (ui.view==View::Saved) {
    if (!s.savedCount && x>=12 && x<228 && y>=132 && y<175) startScan(ui,s);
    else if (x>=7 && x<233 && y>=61 && y<241) {
      if (!ui.renderedSavedReady || ui.renderedSavedGeneration!=s.wifiGeneration) return Action::None;
      const uint8_t row=(y-61)/36;
      if (row<s.savedCount && row<5 && (y-61)%36<33) {
        strncpy(ui.ssid,s.savedSsids[row],sizeof(ui.ssid)-1); ui.ssid[32]=0;
        ui.selectionReturn=View::Saved; ui.securedNetwork=false;
        ui.hint[0]=0; ui.view=View::Network;
      }
    }
    return Action::None;
  }
  if (ui.view==View::Network) {
    if (x<12 || x>=228 || !saved(s,ui.ssid)) return Action::None;
    if (s.wifiBusy) { hint(ui,"WLAN busy. Please wait."); return Action::None; }
    if (y>=116 && y<158) {
      if (EspMessageService::connectSaved(ui.ssid)) { showWifi(ui); hint(ui,"Connection requested"); }
      else hint(ui,"Could not connect. Retry");
    } else if (y>=167 && y<209) startEntry(ui,View::Network);
    else if (y>=218 && y<260) { ui.view=View::Forget; ui.hint[0]=0; }
    return Action::None;
  }
  if (ui.view==View::Forget) {
    if (x<12 || x>=228) return Action::None;
    if (y>=179 && y<219) { ui.view=View::Network; ui.hint[0]=0; }
    else if (y>=230 && y<270) {
      if (s.wifiBusy) hint(ui,"WLAN busy. Please wait.");
      else if (EspMessageService::forgetWifi(ui.ssid)) {
        clearSecret(ui); ui.view=View::Saved; ui.renderedSavedReady=false;
        hint(ui,"Network removal requested");
      } else hint(ui,"Could not remove. Retry");
    }
    return Action::None;
  }
  if (ui.view==View::Scan) {
    if (x>=180 && y<35 && !s.scanning && !ui.awaitScan && s.scanCount>4) {
      ui.scanPage=(ui.scanPage+1)%((s.scanCount+3)/4); ui.renderedScanReady=false;
    }
    else if (y>=57 && y<225) {
      if (x<7 || x>=233 || s.scanning || ui.awaitScan || !ui.renderedScanReady ||
          ui.renderedScanGeneration!=s.scanGeneration || ui.renderedScanPage!=ui.scanPage) return Action::None;
      const uint8_t index=ui.scanPage*4+(y-57)/42;
      if (index<s.scanCount && index<12 && (y-57)%42<39) {
        strncpy(ui.ssid,s.scanSsids[index],sizeof(ui.ssid)-1); ui.ssid[32]=0;
        ui.securedNetwork=!s.scanOpen[index];
        clearSecret(ui); ui.reveal=false;
        ui.hint[0]=0;
        if (saved(s,ui.ssid)) {
          ui.selectionReturn=View::Scan; ui.view=View::Network;
        } else if (s.savedCount>=5) hint(ui,"Saved list full. Forget one.");
        else if (s.scanOpen[index]) {
          if (EspMessageService::saveWifi(ui.ssid,"")) { showWifi(ui); hint(ui,"Connection requested"); }
          else hint(ui,"Could not save. Retry");
        } else startEntry(ui,View::Scan);
      }
    } else if (y>=239) {
      if (x<79) startScan(ui,s);
      else if (x<159) {
        startEntry(ui,View::Scan); ui.ssid[0]=0; ui.securedNetwork=false; ui.view=View::Ssid;
      }
      else { ui.portalReturn=View::Scan; ui.view=View::Portal; EspMessageService::requestProvision(); }
    }
    return Action::None;
  }
  if (ui.view==View::Ssid || ui.view==View::Password) {
    const uint32_t now=millis();
    EspNineKeyInput::tick(ui.keyboard,now);
    if (ui.view==View::Password && x>=176 && y<35) {
      EspNineKeyInput::commit(ui.keyboard);
      ui.reveal=!ui.reveal;
      return Action::None;
    }
    char *field=ui.view==View::Ssid?ui.ssid:ui.password;
    const size_t capacity=ui.view==View::Ssid?sizeof(ui.ssid):sizeof(ui.password);
    if (y>=70 && y<184) {
      const uint8_t key=((y-70)/38)*3+x/80+1;
      if (EspNineKeyInput::press(ui.keyboard,field,capacity,key,now)) ui.hint[0]=0;
      else if (strlen(field)+1>=capacity) hint(ui,"Input is full");
    } else if (y>=184 && y<222) {
      if (x<80) EspNineKeyInput::caps(ui.keyboard);
      else if (x<160) {
        if (EspNineKeyInput::spaceOrZero(ui.keyboard,field,capacity)) ui.hint[0]=0;
        else hint(ui,"Input is full");
      } else { EspNineKeyInput::erase(ui.keyboard,field); ui.hint[0]=0; }
    } else if (y>=234) {
      EspNineKeyInput::commit(ui.keyboard);
      if (x<59) EspNineKeyInput::setMode(ui.keyboard,EspNineKeyInput::Mode::Letters);
      else if (x<119) EspNineKeyInput::setMode(ui.keyboard,EspNineKeyInput::Mode::Digits);
      else if (x<178) EspNineKeyInput::nextSymbols(ui.keyboard);
      else if (ui.view==View::Ssid) {
        if (!EspProvisionInput::validSsid(ui.ssid)) hint(ui,"SSID: 1-32 bytes");
        else if (saved(s,ui.ssid)) {
          clearSecret(ui); ui.selectionReturn=View::Scan; ui.view=View::Network; ui.hint[0]=0;
        }
        else if (s.savedCount>=5) hint(ui,"Saved list full. Forget one.");
        else {
          ui.view=View::Password; ui.hint[0]=0; ui.reveal=false;
          ui.keyboard=EspNineKeyInput::State{};
        }
      } else if (ui.securedNetwork && !ui.password[0]) hint(ui,"Enter WLAN password");
      else if (!EspProvisionInput::validPassword(ui.password)) hint(ui,"8-63 ASCII / 64 hex");
      else if (s.savedCount>=5 && !saved(s,ui.ssid)) hint(ui,"Saved list full. Forget one.");
      else if (EspMessageService::saveWifi(ui.ssid,ui.password)) {
        showWifi(ui); hint(ui,"Connection requested");
      } else hint(ui,"Could not save. Retry");
    }
    return Action::None;
  }
  if (ui.view==View::Pair && y>=84 && y<240 && x>=9 && x<230) {
    const int col=(x-9)/75,row=(y-84)/39;
    if ((x-9)%75>=71 || col>=3) return Action::None;
    const int index=row*3+col;
    static constexpr char keys[12]={'1','2','3','4','5','6','7','8','9','<','0','G'};
    if (index<0 || index>=12) return Action::None;
    const char key=keys[index];
    if (key=='<') EspProvisionInput::erase(ui.code);
    else if (key=='G') {
      if (s.wifi && EspProvisionInput::validCode(ui.code) && EspMessageService::requestPair(ui.code))
        memset(ui.code,0,sizeof(ui.code));
    } else EspProvisionInput::append(ui.code,sizeof(ui.code),key);
  }
  return Action::None;
}
inline void offlineBadge(Arduino_Canvas& c) {
  c.fillRoundRect(10,248,61,22,6,0);
  c.drawRoundRect(10,248,61,22,6,0x8410);
  text(c,17,256,"OFFLINE",0xffff);
}
}
