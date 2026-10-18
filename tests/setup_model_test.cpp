#include "model.h"
#include <cassert>
#include <cstring>
using namespace esphome::odelic_setup;
int main() {
  FixtureConfig config;
  uint64_t mac;
  assert(make_config("10:20:30:40:50:60", "00010007", config));
  assert(valid_config(config));
  assert(std::string(config.lighting_id) == "00010007");
  assert(address(config) == 0x102030405060ULL);
  assert(!make_config("00:00:00:00:00:00", "00010007", config));
  assert(!make_config("FF:FF:FF:FF:FF:FF", "00010007", config));
  assert(make_config("11:20:30:40:50:60", "00010007", config)); // BLE addresses do not use Ethernet multicast-bit rules
  for (auto text : {"10:20:30:40:50", "10:20:30:40:50:GG", "10-20-30-40-50-60", " 10:20:30:40:50:60"})
    assert(!parse_mac(text, mac));
  assert(parse_mac("ec:c5:7f:01:02:03", mac) && format_mac(mac) == "EC:C5:7F:01:02:03");
  for (auto id : {"1234567", "123456789", "1234abcd", " 12345678", "１２３４５６７８"}) assert(!valid_id(id));
  make_config("10:20:30:40:50:60", "00010007", config);
  config.version = 2; assert(!valid_config(config));
  config.version = 1; config.lighting_id[8] = '9'; assert(!valid_config(config));
  Candidate target{0x102030405060ULL, "same-name", -80}, neighbour{0x102030405062ULL, "same-name", -40};
  std::array<Scan,6> scans{Scan{target,neighbour},Scan{target,neighbour},Scan{neighbour},Scan{neighbour},Scan{target,neighbour},Scan{target,neighbour}};
  assert(compare(scans).size() == 1 && compare(scans)[0].mac == target.mac);
  scans[3].push_back(target); assert(compare(scans).empty()); // transient sighting during OFF disqualifies
  scans[3] = {neighbour}; scans[5] = {neighbour}; assert(compare(scans).empty());
  scans[2] = {}; scans[3] = {}; scans[5] = {target,neighbour}; assert(compare(scans).size() == 2); // ambiguous
  assert(name_hint(0xecc57f010203ULL, ""));
  assert(name_hint(0x102030405060ULL, "PLTCEOC-05"));
  assert(!name_hint(0x102030405060ULL, "unrelated"));
  assert(json_string("\"\\\n<script>") == "\"\\\"\\\\\\u000a<script>\"");
  RosterConfig roster;
  for (size_t i = 0; i < MAX_LIGHTS; ++i) {
    assert(upsert(roster, 0, "Room " + std::to_string(i), format_mac(0x102030405060ULL + i), "00010007", MAX_LIGHTS).empty());
    assert(valid_roster(roster));
  }
  assert(light_count(roster) == 8);
  assert(!upsert(roster, 0, "Overflow", "10:20:30:40:50:80", "00010007", 8).empty());
  const auto old_uid = roster.lights[2].uid;
  assert(upsert(roster, old_uid, "居間", format_mac(address(roster.lights[2].fixture)), "", 8).empty());
  assert(roster.lights[2].uid == old_uid && std::string(roster.lights[2].fixture.lighting_id) == "00010007");
  assert(json_string("居間") == "\"居間\"");
  assert(!valid_name(std::string(65, 'x')) && !valid_name("   ") && !valid_name("Room\n"));
  assert(!valid_utf8(std::string("\xc0\xaf", 2)) && !valid_utf8(std::string("\xed\xa0\x80", 3)));
  assert(!upsert(roster, old_uid, "Duplicate", format_mac(address(roster.lights[0].fixture)), "00010007", 8).empty());
  assert(remove_light(roster, old_uid) && !remove_light(roster, old_uid));
  assert(upsert(roster, 0, "New fixture", "10:20:30:40:50:80", "00010007", 8).empty());
  assert(roster.lights[2].uid == 9 && valid_roster(roster));
  assert(!valid_roster(roster, 2));
  auto migrated = migrate(config);
  assert(!light_count(migrated)); // Earlier test deliberately corrupted the legacy terminator.
  make_config("10:20:30:40:50:60", "00010007", config);
  migrated = migrate(config);
  assert(valid_roster(migrated) && light_count(migrated) == 1 && migrated.next_uid == 2);

}
