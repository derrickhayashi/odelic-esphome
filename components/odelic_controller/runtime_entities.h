#pragma once
#include <cstdint>
#include "esphome/core/device.h"
#include "esphome/components/light/light_state.h"
#include "esphome/components/odelic/odelic.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome::odelic_controller {
class RosterLightState final : public light::LightState {
 public:
  explicit RosterLightState(light::LightOutput *output) : light::LightState(output) {}
  void bind_device(Device *device) { device_ = device; }
};
class RosterPreset final : public odelic::OdelicPreset {
 public:
  void bind_device(Device *device) { device_ = device; }
};
class RosterValid final : public binary_sensor::BinarySensor {
 public:
  void bind_device(Device *device) { device_ = device; }
};
}  // namespace esphome::odelic_controller
