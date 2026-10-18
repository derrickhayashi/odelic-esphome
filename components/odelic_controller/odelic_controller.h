#pragma once
#include "runtime_entities.h"
#include "esphome/core/component.h"
#include "esphome/components/odelic/odelic.h"
#include <array>

namespace esphome::odelic_controller {
constexpr size_t MAX_LIGHTS = 8;

class OdelicController : public Component, public odelic::FixtureCommandSink {
 public:
  void set_capacity(size_t capacity) { capacity_ = capacity; }
  void set_hub(odelic::OdelicHub *hub) { hub_ = hub; }
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
  int active_slot() const { return active_slot_; }
  bool transport_idle() const { return hub_->parent()->state() == esp32_ble_tracker::ClientState::IDLE; }

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
    odelic::OdelicLight *output{nullptr};
    RosterLightState *state{nullptr};
    RosterPreset *preset{nullptr};
    RosterValid *valid{nullptr};
    odelic::protocol::Intent queued{};
    uint32_t queued_at{0}, seen_at{0}, visited_at{0};
    bool seen{false}, visited{false};
  };
  size_t record_count_() const;
  void register_entities_();
  void schedule_();
  odelic::OdelicHub *hub_{nullptr};
  std::array<Record, MAX_LIGHTS> records_{};
  std::array<RuntimeSlot, MAX_LIGHTS> slots_{};
  size_t capacity_{MAX_LIGHTS}, next_slot_{0};
  int active_slot_{-1};
  uint32_t visit_at_{0}, read_at_{0}, observed_revision_{0};
  bool current_read_{false}, initialized_{false}, paused_{true};
};
}  // namespace esphome::odelic_controller
