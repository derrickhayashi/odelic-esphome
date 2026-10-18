#pragma once
#include "model.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/odelic_controller/odelic_controller.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <mutex>
#include <atomic>

namespace esphome::odelic_setup {
class OdelicSetup : public Component, public esp32_ble_tracker::ESPBTDeviceListener, public AsyncWebHandler {
 public:
  void set_capacity(size_t capacity) { capacity_ = capacity; }
  void set_controller(odelic_controller::OdelicController *controller) { controller_ = controller; }
  void set_tracker(esp32_ble_tracker::ESP32BLETracker *tracker) { tracker_ = tracker; }
  void set_credentials(const std::string &user, const std::string &password) {
    user_ = user;
    password_ = password;
  }
  void set_preference_key(uint32_t key) { preference_key_ = key; }
  float get_setup_priority() const override { return setup_priority::LATE + 1.0f; }
  void setup() override;
  void loop() override;
  bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;
  void on_scan_end() override;
  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;

 protected:
  enum class Capture { IDLE, STOPPING, STARTING, RUNNING, FINISHED };
  void action_(const std::string &action, const std::string &mac, const std::string &id, const std::string &name = "",
               uint32_t uid = 0, uint32_t revision = UINT32_MAX);
  void pause_control_();
  void save_roster_(const RosterConfig &next);
  void begin_phase_();
  void finish_scan_();
  void publish_();
  void cancel_(const std::string &message);
  void reply_(AsyncWebServerRequest *request, int code, const char *type, const std::string &body);
  odelic_controller::OdelicController *controller_{nullptr};
  esp32_ble_tracker::ESP32BLETracker *tracker_{nullptr};
  ESPPreferenceObject preference_;
  RosterConfig roster_{};
  size_t capacity_{MAX_LIGHTS};
  uint32_t preference_key_{0};
  std::atomic<bool> action_pending_{false};
  bool restarting_{false}, interrupted_{false}, storage_locked_{false};
  bool wizard_{false}, overflow_{false};
  uint32_t step_at_{0}, scan_at_{0}, wizard_at_{0}, advertisements_{0};
  size_t phase_{0}, sample_{0};
  Capture capture_{Capture::IDLE};
  std::array<Scan, DISCOVERY_SCAN_COUNT> scans_;
  Scan candidates_, current_;
  std::string user_, password_, token_, message_{"Enter details or start discovery."};
  std::mutex snapshot_mutex_;
  std::string snapshot_;
};
}  // namespace esphome::odelic_setup
