#pragma once
#include "runtime_entities.h"
#include "esphome/core/component.h"
#include "esphome/components/odelic/odelic.h"
#include <array>

namespace esphome::odelic_controller {
// Roster/storage compatibility limit; independent BLE sessions are reserved at build time.
constexpr size_t MAX_LIGHTS = 8;

class OdelicController : public Component, public odelic::FixtureCommandSink {
 public:
  void set_capacity(size_t capacity) { this->capacity_ = capacity; }
  void set_slot_hub(size_t slot, odelic::OdelicHub *hub) { this->slots_[slot].hub = hub; }
  void set_slot(size_t slot, odelic::OdelicLight *output, RosterLightState *state, RosterPreset *preset,
                RosterValid *valid);
  void set_record(size_t slot, uint32_t uid, const std::string &name, uint64_t mac, const std::string &lighting_id,
                  uint8_t unit, bool root_device = false);
  void clear_records();
  void light_command(size_t slot, bool on, float brightness) override;
  void preset_command(size_t slot, size_t preset) override;
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void pause();
  void resume();
  bool transport_idle() const;

 protected:
  struct Record {
    uint32_t uid{0};
    std::string name;
    uint64_t mac{0};
    std::string lighting_id;
    uint8_t unit{0};
    bool root_device{false};
  };
  struct RuntimeSlot {
    Device device;
    odelic::OdelicHub *hub{nullptr};
    odelic::OdelicLight *output{nullptr};
    RosterLightState *state{nullptr};
    RosterPreset *preset{nullptr};
    RosterValid *valid{nullptr};
    odelic::protocol::Intent queued{};
    uint32_t queued_at{0}, seen_at{0}, observed_revision{0};
    bool started{false}, seen{false};
  };
  void register_entities_();
  std::array<Record, MAX_LIGHTS> records_{};
  std::array<RuntimeSlot, MAX_LIGHTS> slots_{};
  size_t capacity_{MAX_LIGHTS};
  bool initialized_{false}, paused_{true};
};
}  // namespace esphome::odelic_controller
