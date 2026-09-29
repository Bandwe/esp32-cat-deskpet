#include <assert.h>
#include <string.h>
#include "../../sketch/DeskPetV01/EspProvisionInput.h"
#include "../../sketch/DeskPetV01/EspMessageProtocol.h"

int main() {
  using namespace EspProvisionInput;
  assert(validSsid("Home-WiFi"));
  assert(!validSsid(""));
  assert(validPassword("")); // Open network.
  assert(!validPassword("1234567"));
  assert(validPassword("12345678"));
  char hex[65]; memset(hex,'A',64); hex[64]=0;
  assert(validPassword(hex));
  hex[63]='G'; assert(!validPassword(hex));
  assert(validCode("12345678"));
  assert(!validCode("1234567a"));
  assert(!validCode("123456789"));
  assert(sameScanEntry("Guest",0,"Guest",0));
  assert(!sameScanEntry("Guest",0,"Guest",3)); // Open/secured variants are separate.
  assert(!sameScanEntry("Guest",3,"Guest-2",3));
  char value[9]={};
  for (int i=0;i<8;++i) assert(append(value,sizeof(value),'a'));
  assert(!append(value,sizeof(value),'b'));
  erase(value); assert(strlen(value)==7);
  assert(keyAt(0,70,Page::Letters,false)=='q');
  assert(keyAt(239,229,Page::Letters,false)==' ');
  assert(keyAt(0,70,Page::Numbers,false)=='1');
  assert(keyAt(60,102,Page::Letters,true)=='I');
  assert(keyAt(40,102,Page::More,false)==96); // Backtick.
  assert(!keyAt(240,70,Page::Letters,false));
  assert(!keyAt(0,230,Page::Letters,false));
  bool printableAscii[95]={};
  for (unsigned p=0;p<3;++p) for (unsigned row=0;row<Rows;++row)
    for (unsigned col=0;col<Columns;++col) for (unsigned upper=0;upper<2;++upper) {
      const char c=key(row,col,static_cast<Page>(p),upper!=0);
      if (c>=32 && c<=126) printableAscii[c-32]=true;
    }
  for (bool reachable:printableAscii) assert(reachable);

  char token[129]={};
  assert(EspMessageProtocol::parsePairToken("{\"deviceToken\":\"abcdefghijklmnop\"}",token));
  assert(!strcmp(token,"abcdefghijklmnop"));
  assert(!EspMessageProtocol::parsePairToken("{\"deviceToken\":\"short\"}",token));
  assert(!EspMessageProtocol::parsePairToken("{\"deviceToken\":\"abcdefghijklmnop\",\"other\":1}",token));
  assert(!EspMessageProtocol::parsePairToken("{\"deviceToken\":\"abc\\ndefghijklmnop\"}",token));
  assert(!EspMessageProtocol::parsePairToken("{\"deviceToken\":\"abcdefghijklmnop\"}garbage",token));
}
