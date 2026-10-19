#include "odelic_controller.h"
#include "esphome/core/application.h"

namespace esphome::odelic_controller {
namespace {
// Application policies: expire user intents and invalidate silent/offline fixtures.
constexpr uint32_t COMMAND_TTL_MS = 60'000;
constexpr uint32_t STATE_STALE_MS = 30'000;
}  // namespace

void OdelicController::set_slot(size_t slot, odelic::OdelicLight *output, RosterLightState *state, RosterPreset *preset,
                                RosterValid *valid) {
  auto &runtime = this->slots_[slot];
  runtime.output = output;
  runtime.state = state;
  runtime.preset = preset;
  runtime.valid = valid;
  // Each slot owns its authentication mask, mesh mapping, query and command state.
  // Never share a hub between MACs: login/session state belongs to that connection.
  output->set_parent(runtime.hub);
  output->set_command_sink(this, slot);
  preset->set_parent(runtime.hub);
  preset->set_command_sink(this, slot);
  runtime.hub->set_light(output);
  runtime.hub->set_preset(preset);
  runtime.hub->set_state_valid(valid);
}

void OdelicController::set_record(size_t slot, uint32_t uid, const std::string &name, uint64_t mac,
                                  const std::string &lighting_id, uint8_t unit, bool root_device) {
  if (slot < this->capacity_)
    this->records_[slot] = {uid, name, mac, lighting_id, unit, root_device};
}

void OdelicController::clear_records() { this->records_ = {}; }

void OdelicController::setup() {
  this->pause();
  this->register_entities_();
  this->initialized_ = true;
  this->resume();
}

void OdelicController::register_entities_() {
  for (size_t i = 0; i < this->capacity_; ++i) {
    const auto &record = this->records_[i];
    if (!record.uid)
      continue;
    auto &runtime = this->slots_[i];
    if (!record.root_device) {
      runtime.device.set_device_id(record.uid);
      runtime.device.set_name(record.name.c_str());
      runtime.state->bind_device(&runtime.device);
      runtime.preset->bind_device(&runtime.device);
      runtime.valid->bind_device(&runtime.device);
      App.register_device(&runtime.device);
    }
    App.register_light(runtime.state, record.root_device ? record.name.c_str() : "Light", 0, 0);
    App.register_select(runtime.preset, "White preset", 0, 0);
    App.register_binary_sensor(runtime.valid, "Fixture state valid", 0,
                               uint32_t(ENTITY_CATEGORY_DIAGNOSTIC) << ENTITY_FIELD_ENTITY_CATEGORY_SHIFT);
    runtime.valid->publish_state(false);
  }
}

void OdelicController::pause() {
  this->paused_ = true;
  for (size_t i = 0; i < this->capacity_; ++i) {
    auto &runtime = this->slots_[i];
    // Discovery/storage changes must stop ALL transports, including unused slots,
    // and clear both controller queues and already-dispatched hub intents.
    runtime.hub->suspend_for_setup();
    runtime.queued = {};
    runtime.started = runtime.seen = false;
    if (this->records_[i].uid)
      runtime.valid->publish_state(false);
  }
}

void OdelicController::resume() { this->paused_ = false; }

bool OdelicController::transport_idle() const {
  for (size_t i = 0; i < this->capacity_; ++i)
    if (this->slots_[i].hub->parent()->state() != esp32_ble_tracker::ClientState::IDLE)
      return false;
  return true;
}

void OdelicController::light_command(size_t slot, bool on, float brightness) {
  if (this->paused_ || slot >= this->capacity_ || !this->records_[slot].uid)
    return;
  this->slots_[slot].queued = {odelic::protocol::Kind::LIGHT, on, odelic::protocol::quantize(brightness), 0};
  this->slots_[slot].queued_at = millis();
}

void OdelicController::preset_command(size_t slot, size_t preset) {
  if (this->paused_ || slot >= this->capacity_ || !this->records_[slot].uid || preset >= 4)
    return;
  this->slots_[slot].queued = {odelic::protocol::Kind::PRESET, true, 20, odelic::protocol::PRESETS[preset]};
  this->slots_[slot].queued_at = millis();
}

void OdelicController::loop() {
  if (!this->initialized_ || this->paused_)
    return;
  const uint32_t now = millis();
  for (size_t i = 0; i < this->capacity_; ++i) {
    if (!this->records_[i].uid)
      continue;
    auto &runtime = this->slots_[i];
    auto *hub = runtime.hub;
    if (runtime.queued.kind != odelic::protocol::Kind::NONE && now - runtime.queued_at >= COMMAND_TTL_MS)
      runtime.queued = {};
    if (!runtime.started) {
      // Wait for asynchronous CLOSE/DISCONNECT before resuming a paused session.
      if (hub->parent()->state() != esp32_ble_tracker::ClientState::IDLE)
        continue;
      const auto &record = this->records_[i];
      hub->parent()->set_address(record.mac);
      hub->set_lighting_id(record.lighting_id);
      hub->set_unit(record.unit);
      runtime.observed_revision = hub->state_revision();
      runtime.started = true;
      hub->resume_after_setup();
    }
    if (hub->state_revision() != runtime.observed_revision && hub->actual().known) {
      runtime.observed_revision = hub->state_revision();
      runtime.seen = true;
      runtime.seen_at = now;
      runtime.valid->publish_state(true);
    }
    if (!hub->actual().known || (runtime.seen && now - runtime.seen_at > STATE_STALE_MS)) {
      runtime.seen = false;
      runtime.valid->publish_state(false);
    }
    // Hub requests force another MAC-matched query before actuation. A cached
    // valid flag alone never authorizes a write, including after reconnect.
    if (runtime.seen && hub->ready() && !hub->pending() && runtime.queued.kind != odelic::protocol::Kind::NONE) {
      auto request = runtime.queued;
      runtime.queued = {};
      if (request.kind == odelic::protocol::Kind::LIGHT)
        hub->request_light(request.on, request.brightness / float(odelic::protocol::BRIGHTNESS_STEPS));
      else
        for (size_t preset = 0; preset < 4; ++preset)
          if (odelic::protocol::PRESETS[preset] == request.color)
            hub->request_preset(preset);
    }
    // Keep healthy sessions connected. ESPHome reconnects each failed client
    // independently; no offline fixture or busy slider gates another hub's loop.
  }
}
}  // namespace esphome::odelic_controller
