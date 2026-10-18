#include "odelic_controller.h"
#include "esphome/core/application.h"

namespace esphome::odelic_controller {
namespace {
constexpr uint32_t COMMAND_TTL_MS = 60'000;
constexpr uint32_t VISIT_BUDGET_MS = 20'000;
constexpr uint32_t POLL_ALLOWANCE_PER_LIGHT_MS = 25'000;
constexpr uint32_t STATE_VALIDITY_GRACE_MS = 30'000;
constexpr uint32_t IDLE_DWELL_MS = 1'500;
}  // namespace

void OdelicController::set_slot(size_t slot, odelic::OdelicLight *output, RosterLightState *state, RosterPreset *preset,
                                RosterValid *valid) {
  auto &runtime = slots_[slot];
  runtime.output = output;
  runtime.state = state;
  runtime.preset = preset;
  runtime.valid = valid;
  output->set_parent(hub_);
  output->set_command_sink(this, slot);
  preset->set_parent(hub_);
  preset->set_command_sink(this, slot);
}

void OdelicController::set_record(size_t slot, uint32_t uid, const std::string &name, uint64_t mac,
                                  const std::string &lighting_id, uint8_t unit, bool root_device) {
  if (slot >= capacity_)
    return;
  records_[slot] = {uid, name, mac, lighting_id, unit, root_device};
}

void OdelicController::clear_records() { records_ = {}; }

size_t OdelicController::record_count_() const {
  size_t count = 0;
  for (size_t i = 0; i < capacity_; ++i)
    if (records_[i].uid)
      ++count;
  return count;
}

void OdelicController::setup() {
  hub_->suspend_for_setup();
  register_entities_();
  initialized_ = true;
  paused_ = false;
}

void OdelicController::register_entities_() {
  for (size_t i = 0; i < capacity_; ++i) {
    const auto &record = records_[i];
    if (!record.uid)
      continue;
    auto &runtime = slots_[i];
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
  hub_->set_light(nullptr);
  hub_->set_preset(nullptr);
  hub_->set_state_valid(nullptr);
  hub_->suspend_for_setup();
  active_slot_ = -1;
  current_read_ = false;
  paused_ = true;
  for (size_t i = 0; i < capacity_; ++i) {
    slots_[i].queued = {};
    slots_[i].seen = false;
    if (slots_[i].valid != nullptr && records_[i].uid)
      slots_[i].valid->publish_state(false);
  }
}

void OdelicController::resume() {
  next_slot_ = 0;
  paused_ = false;
}

void OdelicController::light_command(size_t slot, bool on, float brightness) {
  if (paused_ || slot >= capacity_ || !records_[slot].uid)
    return;
  slots_[slot].queued = {odelic::protocol::Kind::LIGHT, on, odelic::protocol::quantize(brightness), 0};
  slots_[slot].queued_at = millis();
}

void OdelicController::preset_command(size_t slot, size_t preset) {
  if (paused_ || slot >= capacity_ || !records_[slot].uid || preset >= 4)
    return;
  slots_[slot].queued = {odelic::protocol::Kind::PRESET, true, 20, odelic::protocol::PRESETS[preset]};
  slots_[slot].queued_at = millis();
}

void OdelicController::loop() {
  if (!initialized_ || paused_ || record_count_() == 0)
    return;
  schedule_();
}

void OdelicController::schedule_() {
  const uint32_t now = millis();
  const size_t count = record_count_();
  const uint32_t stale_after = uint32_t(count) * POLL_ALLOWANCE_PER_LIGHT_MS + STATE_VALIDITY_GRACE_MS;
  for (size_t i = 0; i < capacity_; ++i) {
    auto &runtime = slots_[i];
    if (runtime.queued.kind != odelic::protocol::Kind::NONE && now - runtime.queued_at >= COMMAND_TTL_MS)
      runtime.queued = {};
    if (runtime.seen && now - runtime.seen_at > stale_after) {
      runtime.seen = false;
      runtime.valid->publish_state(false);
    }
  }
  if (active_slot_ < 0) {
    if (hub_->parent()->state() != esp32_ble_tracker::ClientState::IDLE)
      return;
    int selected = -1;
    for (unsigned pass = 0; pass < 3 && selected < 0; ++pass)
      for (size_t n = 0; n < capacity_; ++n) {
        size_t i = (next_slot_ + n) % capacity_;
        const auto &runtime = slots_[i];
        bool due = !runtime.visited || now - runtime.visited_at >= uint32_t(count) * POLL_ALLOWANCE_PER_LIGHT_MS;
        bool choose = pass == 0 ? due : pass == 1 ? runtime.queued.kind != odelic::protocol::Kind::NONE : true;
        if (records_[i].uid && choose) {
          selected = int(i);
          break;
        }
      }
    if (selected < 0)
      return;
    active_slot_ = selected;
    const auto &record = records_[selected];
    auto &runtime = slots_[selected];
    hub_->parent()->set_address(record.mac);
    hub_->set_lighting_id(record.lighting_id);
    hub_->set_unit(record.unit);
    hub_->set_light(runtime.output);
    hub_->set_preset(runtime.preset);
    observed_revision_ = hub_->state_revision();
    runtime.visited = true;
    runtime.visited_at = now;
    current_read_ = false;
    visit_at_ = now;
    hub_->resume_after_setup();
    return;
  }
  auto &runtime = slots_[active_slot_];
  if (hub_->state_revision() != observed_revision_ && hub_->actual().known) {
    observed_revision_ = hub_->state_revision();
    current_read_ = true;
    read_at_ = now;
    runtime.seen = true;
    runtime.seen_at = now;
    runtime.valid->publish_state(true);
  }
  if (current_read_ && hub_->ready() && !hub_->pending() && runtime.queued.kind != odelic::protocol::Kind::NONE) {
    auto request = runtime.queued;
    runtime.queued = {};
    if (request.kind == odelic::protocol::Kind::LIGHT)
      hub_->request_light(request.on, request.brightness / float(odelic::protocol::BRIGHTNESS_STEPS));
    else
      for (size_t i = 0; i < 4; ++i)
        if (odelic::protocol::PRESETS[i] == request.color)
          hub_->request_preset(i);
    read_at_ = now;
  }
  const bool timed_out = now - visit_at_ >= VISIT_BUDGET_MS && (count > 1 || !current_read_ || !hub_->actual().known);
  const bool rotate = count > 1 && current_read_ && hub_->ready() && !hub_->pending() &&
                      runtime.queued.kind == odelic::protocol::Kind::NONE && now - read_at_ >= IDLE_DWELL_MS;
  if (timed_out || rotate) {
    if (timed_out) {
      runtime.queued = {};
      if (!current_read_ || !hub_->actual().known || hub_->pending() || !hub_->ready()) {
        runtime.seen = false;
        runtime.valid->publish_state(false);
      }
    }
    next_slot_ = (size_t(active_slot_) + 1) % capacity_;
    hub_->set_light(nullptr);
    hub_->set_preset(nullptr);
    hub_->suspend_for_setup();
    active_slot_ = -1;
    current_read_ = false;
  }
}
}  // namespace esphome::odelic_controller
