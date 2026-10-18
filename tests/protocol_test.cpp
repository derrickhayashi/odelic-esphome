#include "protocol.h"
#include <cassert>
#include <cstdio>
using namespace esphome::odelic::protocol;
int main() {
  uint8_t address[]={1,0,0,0},sender[]={3,0,0,0};
  Status s;
  uint8_t raw[]={1,0,0,0,0x71,4,4};
  assert(parse_status(raw,sizeof(raw),address,s) && s.on && s.levels && s.dimming==4);
  Intent brightness{Kind::LIGHT,true,10,0};
  auto change=plan(s,brightness);
  assert(change.action==Action::SET && change.color==4 && change.dimming==10);
  Intent preset{Kind::PRESET,true,20,12};
  change=plan(s,preset);
  assert(change.action==Action::SET && change.color==12 && change.dimming==4);
  raw[6]=20; assert(!parse_status(raw,sizeof(raw),address,s));
  raw[0]=2; raw[6]=4; assert(!parse_status(raw,sizeof(raw),address,s));
  raw[0]=1; raw[5]=raw[6]=0x32;
  assert(parse_status(raw,sizeof(raw),address,s) && !s.on);
  assert(plan(s,preset).action==Action::REJECT);
  assert(plan(s,brightness).action==Action::POWER_ON);
  raw[5]=raw[6]=0x37; assert(parse_status(raw,sizeof(raw),address,s) && s.on && !s.levels);
  assert(plan(s,preset).action==Action::REJECT);
  assert(plan(s,brightness).action==Action::REJECT);
  Intent off{Kind::LIGHT,false,20,0};
  assert(plan(s,off).action==Action::POWER_OFF);
  s.known=false; assert(plan(s,off).action==Action::REJECT);
  assert(quantize(0.0f)==1 && quantize(0.5f)==10 && quantize(1.0f)==20);
  assert(quantize(0.49f)==10 && quantize(1.1f)==20);
  std::array<uint8_t,18> packet;
  for (auto color:PRESETS) {
    for (uint8_t d=0;d<20;d++) {
      assert(setting(address,sender,2,color,d,packet));
      assert(packet[0]==3 && packet[1]==1 && packet[5]==0x20 && packet[6]==3);
      assert(packet[10]==0xc0 && packet[11]==2 && packet[12]==color && packet[13]==d);
    }
  }
  uint8_t broadcast[]={255,255,255,255};
  assert(!setting(broadcast,sender,0,0,0,packet));
  assert(!setting(address,sender,0,21,0,packet));
  assert(!setting(address,sender,0,0,20,packet));
  assert(setting(address,sender,0,0x32,0x32,packet));
  puts("Passed: addressed commands, status filtering, brightness quantization, preset brightness preservation, OFF/unknown guards");
}
