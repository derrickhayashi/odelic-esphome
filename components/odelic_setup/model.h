#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace esphome::odelic_setup {
constexpr uint32_t FIXTURE_FORMAT_VERSION = 1;
constexpr uint32_t ROSTER_FORMAT_VERSION = 2;
constexpr size_t LIGHTING_ID_DIGITS = 8;
constexpr size_t MAC_OCTETS = 6;
constexpr size_t MAC_TEXT_LENGTH = 17;       // Six hex pairs separated by five colons.
constexpr size_t MAX_LIGHT_NAME_BYTES = 64;  // UTF-8 bytes, not Unicode character count.
constexpr size_t SCANS_PER_PHASE = 2;
constexpr size_t DISCOVERY_PHASE_COUNT = 3;  // Initial ON, wall OFF, restored ON.
constexpr size_t DISCOVERY_SCAN_COUNT = SCANS_PER_PHASE * DISCOVERY_PHASE_COUNT;

// Saved as a binary NVS blob. Keep field order, sizes and padding compatible
// with the original v1 firmware; changing the layout needs a version/migration.
struct FixtureConfig {
  uint32_t version{FIXTURE_FORMAT_VERSION};
  uint8_t mac[MAC_OCTETS]{};
  char lighting_id[LIGHTING_ID_DIGITS + 1]{};
};
inline bool valid_id(const std::string &id) {
  return id.size() == LIGHTING_ID_DIGITS &&
         std::all_of(id.begin(), id.end(), [](char c) { return c >= '0' && c <= '9'; });
}
inline bool parse_mac(const std::string &text, uint64_t &result) {
  if (text.size() != MAC_TEXT_LENGTH)
    return false;
  result = 0;
  for (size_t i = 0; i < MAC_TEXT_LENGTH; ++i) {
    if (i % 3 == 2) {
      if (text[i] != ':')
        return false;
      continue;
    }
    char c = text[i];
    int nibble = c >= '0' && c <= '9'   ? c - '0'
                 : c >= 'a' && c <= 'f' ? c - 'a' + 10
                 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                        : -1;
    if (nibble < 0)
      return false;
    result = (result << 4) | nibble;
  }
  return result != 0 && result != 0xffffffffffffULL;
}
inline uint64_t address(const FixtureConfig &config) {
  uint64_t mac = 0;
  for (uint8_t octet : config.mac)
    mac = (mac << 8) | octet;
  return mac;
}
inline std::string format_mac(uint64_t mac) {
  char text[18];
  snprintf(text, sizeof(text), "%02X:%02X:%02X:%02X:%02X:%02X", unsigned((mac >> 40) & 255),
           unsigned((mac >> 32) & 255), unsigned((mac >> 24) & 255), unsigned((mac >> 16) & 255),
           unsigned((mac >> 8) & 255), unsigned(mac & 255));
  return text;
}
inline bool valid_config(const FixtureConfig &config) {
  uint64_t mac;
  return config.version == FIXTURE_FORMAT_VERSION && config.lighting_id[LIGHTING_ID_DIGITS] == 0 &&
         valid_id(std::string(config.lighting_id, LIGHTING_ID_DIGITS)) && parse_mac(format_mac(address(config)), mac);
}
inline bool make_config(const std::string &mac, const std::string &id, FixtureConfig &config) {
  uint64_t value;
  if (!parse_mac(mac, value) || !valid_id(id))
    return false;
  config = {};
  for (size_t i = 0; i < MAC_OCTETS; ++i)
    config.mac[i] = value >> (8 * (MAC_OCTETS - 1 - i));
  std::copy(id.begin(), id.end(), config.lighting_id);
  return true;
}
// Version 1 FixtureConfig stays byte-for-byte compatible for migration.
// Application memory/build bound, not a proven BLE radio capacity. Codegen in
// __init__.py must reserve the same maximum. Raising it also changes the blob size.
constexpr size_t MAX_LIGHTS = 8;
struct LightRecord {
  uint32_t uid{0};  // Never reused: removed entities cannot take over a new fixture.
  FixtureConfig fixture{};
  char name[MAX_LIGHT_NAME_BYTES + 1]{};
  uint8_t unit{0};  // Odelic setting-packet unit field; zero matches the tested fixtures.
};
struct RosterConfig {
  uint32_t version{ROSTER_FORMAT_VERSION};
  uint32_t revision{0};  // Optimistic edit revision; rejects stale browser forms.
  uint32_t next_uid{1};  // Zero means unused; increasing IDs prevent entity identity reuse.
  std::array<LightRecord, MAX_LIGHTS> lights{};
};
// Existing ESP32 NVS blobs include C++ padding. These sizes pin the v1/v2 ABI;
// changing any assertion requires an explicit new format and migration path.
static_assert(sizeof(FixtureConfig) == 20, "Preserve v1 fixture blob layout");
static_assert(sizeof(LightRecord) == 92, "Preserve v2 light record layout");
static_assert(sizeof(RosterConfig) == 748, "Preserve v2 eight-light roster layout");

inline bool valid_utf8(const std::string &text) {
  // Validate standard UTF-8 continuation bytes/code-point ranges. In particular,
  // reject overlong forms, UTF-16 surrogates and values outside Unicode, so stored
  // room labels can be returned in JSON without corrupting the setup page.
  for (size_t i = 0; i < text.size();) {
    uint8_t c = text[i++];
    if (c < 128) {
      if (c < 32 || c == 127)
        return false;
      continue;
    }
    unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
    if (!count || i + count > text.size())
      return false;
    uint32_t code = c & (0x7f >> count);
    for (unsigned n = 0; n < count; ++n) {
      uint8_t next = text[i++];
      if ((next & 0xc0) != 0x80)
        return false;
      code = (code << 6) | (next & 0x3f);
    }
    if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) || (count == 3 && code < 0x10000) ||
        (code >= 0xd800 && code <= 0xdfff) || code > 0x10ffff)
      return false;
  }
  return true;
}
inline bool valid_name(const std::string &name) {
  return !name.empty() && name.size() <= MAX_LIGHT_NAME_BYTES && valid_utf8(name) &&
         name.find_first_not_of(' ') != std::string::npos;
}
inline size_t light_count(const RosterConfig &roster) {
  return std::count_if(roster.lights.begin(), roster.lights.end(), [](const LightRecord &r) { return r.uid != 0; });
}
inline int find_uid(const RosterConfig &roster, uint32_t uid) {
  for (size_t i = 0; i < MAX_LIGHTS; ++i)
    if (uid && roster.lights[i].uid == uid)
      return int(i);
  return -1;
}
inline bool valid_roster(const RosterConfig &roster, size_t capacity = MAX_LIGHTS) {
  if (roster.version != ROSTER_FORMAT_VERSION || !roster.next_uid || capacity > MAX_LIGHTS)
    return false;
  for (size_t i = 0; i < MAX_LIGHTS; ++i) {
    const auto &r = roster.lights[i];
    if (!r.uid)
      continue;
    if (i >= capacity || r.uid >= roster.next_uid || r.name[MAX_LIGHT_NAME_BYTES] || !valid_name(r.name) ||
        !valid_config(r.fixture))
      return false;
    for (size_t j = 0; j < i; ++j)
      if (roster.lights[j].uid &&
          (roster.lights[j].uid == r.uid || address(roster.lights[j].fixture) == address(r.fixture)))
        return false;
  }
  return true;
}
inline std::string upsert(RosterConfig &roster, uint32_t uid, const std::string &name, const std::string &mac,
                          const std::string &id, size_t capacity) {
  if (capacity < 1 || capacity > MAX_LIGHTS)
    return "Invalid light capacity.";
  if (!valid_name(name))
    return "Enter a light name (up to 64 bytes, without control characters).";
  int slot = find_uid(roster, uid);
  if (uid && slot < 0)
    return "That light no longer exists. Reload the list.";
  if (!uid) {
    if (roster.next_uid == UINT32_MAX)
      return "Light identity space exhausted.";
    for (size_t i = 0; i < capacity; ++i)
      if (!roster.lights[i].uid) {
        slot = int(i);
        break;
      }
    if (slot < 0)
      return "This bridge has reached its configured light capacity.";
  }
  LightRecord next{};
  const std::string credential = id.empty() && uid ? roster.lights[slot].fixture.lighting_id : id;
  if (!make_config(mac, credential, next.fixture))
    return "Enter a valid MAC and eight-digit lighting ID.";
  for (size_t i = 0; i < MAX_LIGHTS; ++i)
    if (int(i) != slot && roster.lights[i].uid && address(roster.lights[i].fixture) == address(next.fixture))
      return "That MAC is already configured.";
  // Editing retains the UID even if a label/MAC changes. A newly added record
  // always gets a fresh UID, even when it occupies a previously removed slot.
  next.uid = uid ? uid : roster.next_uid++;
  next.unit = uid ? roster.lights[slot].unit : 0;
  std::copy(name.begin(), name.end(), next.name);
  roster.lights[slot] = next;
  ++roster.revision;
  return "";
}
inline bool remove_light(RosterConfig &roster, uint32_t uid) {
  int slot = find_uid(roster, uid);
  if (slot < 0)
    return false;
  // This changes only the bridge's roster; it does not unpair or reset hardware.
  roster.lights[slot] = {};
  ++roster.revision;
  return true;
}
inline RosterConfig migrate(const FixtureConfig &legacy) {
  RosterConfig result;
  if (valid_config(legacy)) {
    result.lights[0].uid = 1;
    result.lights[0].fixture = legacy;
    std::copy_n("Ceiling light", 14, result.lights[0].name);
    result.next_uid = 2;
  }
  return result;
}
struct Candidate {
  uint64_t mac;
  std::string name;
  int rssi;
};
using Scan = std::vector<Candidate>;
inline bool contains(const Scan &scan, uint64_t mac) {
  return std::any_of(scan.begin(), scan.end(), [mac](const Candidate &c) { return c.mac == mac; });
}
inline bool name_hint(uint64_t mac, const std::string &name) {
  // PLT names and EC:C5:7F were observed on the research fixtures. These are
  // candidate hints, not an exhaustive model filter or proof of ownership.
  return (mac >> 24) == 0xecc57f || name.rfind("PLT", 0) == 0;
}
inline Scan compare(const std::array<Scan, DISCOVERY_SCAN_COUNT> &scans) {
  // Windows are ordered [ON1, ON2, OFF1, OFF2, restored1, restored2]. A transient
  // OFF-phase sighting disqualifies a MAC; it must appear in every ON window.
  // RSSI/name are deliberately excluded from ownership comparison.
  Scan result;
  for (const auto &candidate : scans[0])
    if (contains(scans[1], candidate.mac) && !contains(scans[2], candidate.mac) && !contains(scans[3], candidate.mac) &&
        contains(scans[4], candidate.mac) && contains(scans[5], candidate.mac))
      result.push_back(candidate);
  return result;
}
inline std::string json_string(const std::string &value) {
  std::string result = "\"";
  for (unsigned char c : value) {
    if (c == '"' || c == '\\') {
      result += '\\';
      result += char(c);
    } else if (c < 32) {
      char code[7];
      snprintf(code, sizeof(code), "\\u%04x", c);
      result += code;
    } else
      result += char(c);
  }
  return result + '"';
}
}  // namespace esphome::odelic_setup
