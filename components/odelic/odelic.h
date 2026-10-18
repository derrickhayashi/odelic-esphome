#pragma once
#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/light/light_output.h"
#include "esphome/components/select/select.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "protocol.h"

namespace esphome::odelic {
class OdelicLight;
class OdelicPreset;
class FixtureCommandSink {
 public:
  virtual void light_command(size_t slot, bool on, float brightness) = 0;
  virtual void preset_command(size_t slot, size_t preset) = 0;
};
class OdelicHub : public PollingComponent, public ble_client::BLEClientNode {
 public:
  void setup() override;
  void loop() override;
  void update() override { poll_due_ = true; }
  void dump_config() override;
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t interface,
                           esp_ble_gattc_cb_param_t *param) override;
  void set_lighting_id(const std::string &id);
  void set_unit(uint8_t unit) { unit_ = unit; }
  void set_light(OdelicLight *light) { light_ = light; }
  void set_preset(OdelicPreset *preset) { preset_ = preset; }
  void set_state_valid(binary_sensor::BinarySensor *sensor) { valid_sensor_ = sensor; }
  void suspend_for_setup();
  void resume_after_setup();
  const protocol::Status &actual() const { return actual_; }
  uint32_t state_revision() const { return state_revision_; }
  bool ready() const { return phase_state_ == Phase::READY && actual_.known; }
  bool pending() const { return intent_.kind != protocol::Kind::NONE || write_pending_; }
  void request_light(bool on, float brightness);
  void request_preset(size_t index);

 protected:
  enum class Phase { DISCONNECTED, SUBSCRIBING, CHALLENGE, LOGIN, READY, DISCOVERING, QUERYING, SETTLING };
  void phase_(Phase phase);
  void invalidate_();
  void fail_(const char *reason);
  bool write_(const uint8_t *data, size_t size);
  void notify_(const uint8_t *data, size_t size);
  void start_query_();
  void publish_();
  void apply_();
  bool login_response_(const uint8_t *challenge);
  size_t decrypt_event_(const uint8_t *data, size_t size, uint8_t *plain);
  std::array<uint8_t, 19> login_reply_{};
  bool login_reply_pending_{false};
  std::array<uint8_t, 4> home_{};
  std::array<uint8_t, 4> password_{};
  std::array<uint8_t, 4> mask_{};
  std::array<uint8_t, 4> own_{};
  std::array<uint8_t, 4> fixture_{};
  bool have_mask_{false}, have_own_{false}, have_fixture_{false};
  bool setup_suspended_{false};
  bool write_pending_{false}, poll_due_{true}, fresh_{false};
  uint16_t command_handle_{0}, notify_handle_{0}, cccd_handle_{0};
  uint8_t unit_{0}, attempts_{0};
  uint32_t state_revision_{0};
  uint32_t phase_at_{0}, write_at_{0};
  Phase phase_state_{Phase::DISCONNECTED};
  protocol::Status actual_{};
  protocol::Intent intent_{};
  OdelicLight *light_{nullptr};
  OdelicPreset *preset_{nullptr};
  binary_sensor::BinarySensor *valid_sensor_{nullptr};
};
class OdelicLight : public light::LightOutput {
 public:
  void set_parent(OdelicHub *parent) { parent_ = parent; }
  void set_command_sink(FixtureCommandSink *sink, size_t slot) {
    sink_ = sink;
    slot_ = slot;
  }
  light::LightTraits get_traits() override;
  void setup_state(light::LightState *state) override { state_ = state; }
  void update_state(light::LightState *state) override;
  void write_state(light::LightState *) override {}  // update_state captures the intent synchronously.
  void publish(const protocol::Status &actual);

 protected:
  FixtureCommandSink *sink_{nullptr};
  size_t slot_{0};
  OdelicHub *parent_{nullptr};
  light::LightState *state_{nullptr};
  light::LightColorValues confirmed_{};
  bool have_confirmed_{false};
};
class OdelicPreset : public select::Select {
 public:
  void set_parent(OdelicHub *parent) { parent_ = parent; }
  void set_command_sink(FixtureCommandSink *sink, size_t slot) {
    sink_ = sink;
    slot_ = slot;
  }

 protected:
  void control(size_t index) override {
    if (sink_ != nullptr)
      sink_->preset_command(slot_, index);
    else
      parent_->request_preset(index);
  }
  FixtureCommandSink *sink_{nullptr};
  size_t slot_{0};
  OdelicHub *parent_{nullptr};
};
}  // namespace esphome::odelic
