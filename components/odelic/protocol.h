#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome::odelic::protocol {
// Reconstructed from the app and verified against fixture readback; see the
// research repository's docs/{state-feedback,colour-presets,brightness}.md.
// White steps are fixture-specific protocol indices, not calibrated Kelvin values.
constexpr uint8_t PRESETS[] = {0, 4, 12, 20};
constexpr uint8_t COLOR_STEP_MAX = 20;
constexpr uint8_t BRIGHTNESS_STEPS = 20;  // Steps 1..20 represent 5..100%.
constexpr uint8_t DIMMING_STEP_MAX = 19;  // Wire dimming is inverted: 0=100%, 19=5%.
constexpr uint8_t POWER_OFF_LEVEL = 0x32;
constexpr uint8_t POWER_ON_LEVEL = 0x37;  // Restore power; does not supply colour/brightness levels.
constexpr uint8_t STATUS_OPCODE = 0x71;
constexpr uint8_t SETTING_OPCODE = 0xc0;
constexpr uint8_t DATA_PACKET_TYPE = 0x03;
constexpr uint8_t COMMAND_CHANNEL = 0x20;

struct Status {
  bool known{false};  // No query result is different from a confirmed OFF result.
  bool on{false};
  bool levels{false};  // Power-only readback must not be treated as colour/dimming data.
  uint8_t color{0};
  uint8_t dimming{0};
};
inline uint8_t quantize(float brightness) {
  return static_cast<uint8_t>(
      std::clamp(static_cast<int>(std::lround(brightness * BRIGHTNESS_STEPS)), 1, int(BRIGHTNESS_STEPS)));
}
inline bool parse_status(const uint8_t *payload, size_t size, const uint8_t address[4], Status &out) {
  // Decrypted status layout: [fixture virtual address:4][opcode:1][colour:1][dimming:1].
  // Neighbours can share credentials; matching only the opcode is insufficient.
  if (size < 7 || std::memcmp(payload, address, 4) || payload[4] != STATUS_OPCODE)
    return false;
  const uint8_t c = payload[5], d = payload[6];
  if (c == POWER_OFF_LEVEL && d == POWER_OFF_LEVEL)
    out = {true, false, false, c, d};
  else if (c == POWER_ON_LEVEL && d == POWER_ON_LEVEL)
    out = {true, true, false, c, d};
  else if (c <= COLOR_STEP_MAX && d <= DIMMING_STEP_MAX)
    out = {true, true, true, c, d};
  else
    return false;
  return true;
}
inline bool setting(const uint8_t address[4], const uint8_t sender[4], uint8_t unit, uint8_t color, uint8_t dimming,
                    std::array<uint8_t, 18> &out) {
  // Discovery may use broadcast, but actuation must always address one fixture.
  const uint8_t broadcast[] = {255, 255, 255, 255};
  if (!std::memcmp(address, broadcast, 4))
    return false;
  const bool power = (color == POWER_OFF_LEVEL && dimming == POWER_OFF_LEVEL) ||
                     (color == POWER_ON_LEVEL && dimming == POWER_ON_LEVEL);
  if (!power && (color > COLOR_STEP_MAX || dimming > DIMMING_STEP_MAX))
    return false;
  // Captured app setting frame: type, destination[4], channel, sender[4],
  // setting opcode, unit, colour, dimming, then four zero-filled trailing bytes.
  // The trailing bytes are preserved from the observed layout; their meaning is unknown.
  out = {};
  out[0] = DATA_PACKET_TYPE;
  std::memcpy(out.data() + 1, address, 4);
  out[5] = COMMAND_CHANNEL;
  std::memcpy(out.data() + 6, sender, 4);
  out[10] = SETTING_OPCODE;
  out[11] = unit;
  out[12] = color;
  out[13] = dimming;
  return true;
}
enum class Kind { NONE, LIGHT, PRESET };
// brightness is a 1..20 level; color is a protocol white step, not a percentage.
struct Intent {
  Kind kind{Kind::NONE};
  bool on{false};
  uint8_t brightness{BRIGHTNESS_STEPS};
  uint8_t color{0};
};
enum class Action { DONE, REJECT, POWER_ON, POWER_OFF, SET };
struct Plan {
  Action action;
  uint8_t color{0};
  uint8_t dimming{0};
};
inline Plan plan(const Status &state, const Intent &intent) {
  if (!state.known)
    return {Action::REJECT};
  if (intent.kind == Kind::NONE)
    return {Action::DONE};
  if (intent.kind == Kind::LIGHT) {
    if (!intent.on)
      return {state.on ? Action::POWER_OFF : Action::DONE, POWER_OFF_LEVEL, POWER_OFF_LEVEL};
    if (intent.brightness < 1 || intent.brightness > BRIGHTNESS_STEPS)
      return {Action::REJECT};
    // ON first restores the fixture. A subsequent query obtains its actual white
    // setting before applying brightness, so an ON action cannot invent a colour.
    if (!state.on)
      return {Action::POWER_ON, POWER_ON_LEVEL, POWER_ON_LEVEL};
    if (!state.levels)
      return {Action::REJECT};
    uint8_t dim = BRIGHTNESS_STEPS - intent.brightness;
    return {state.dimming == dim ? Action::DONE : Action::SET, state.color, dim};
  }
  // A preset preserves queried brightness and requires the light already ON.
  if (!state.on || !state.levels || intent.color > COLOR_STEP_MAX)
    return {Action::REJECT};
  return {state.color == intent.color ? Action::DONE : Action::SET, intent.color, state.dimming};
}
}  // namespace esphome::odelic::protocol
