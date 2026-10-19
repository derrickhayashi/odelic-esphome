#include "odelic_setup.h"
#include "page.h"
#include "esphome/core/application.h"
#include <esp_random.h>

namespace esphome::odelic_setup {
namespace {
constexpr uint32_t SAVE_REBOOT_DELAY_MS = 1'500;        // Let the browser receive the save response before reboot.
constexpr uint32_t WIZARD_TIMEOUT_MS = 300'000;         // Resume normal operation if setup is abandoned.
constexpr uint32_t SCAN_LIFECYCLE_TIMEOUT_MS = 20'000;  // Bound disconnect/start/completion failures per sample.
constexpr uint32_t SCAN_DURATION_SECONDS = 5;           // Two fresh windows per wall-switch state.
constexpr uint32_t MIN_SCAN_WINDOW_MS = 4'500;    // Allow loop-observation jitter; reject prematurely stopped scans.
constexpr size_t MAX_DISCOVERY_CANDIDATES = 64;   // Bound RAM use in a crowded apartment building.
constexpr size_t MAX_ADVERTISED_NAME_BYTES = 40;  // Names are hints, not identifiers.
constexpr size_t MAX_FORM_BODY_BYTES = 512;       // Fits an encoded MAC, ID, label, token and revision.
}  // namespace

void OdelicSetup::setup() {
  controller_->pause();
  // ESP32 NVS is keyed, and load() checks the blob length. Use the roster type
  // for normal storage; a separate v1-sized object below handles the old layout.
  preference_ = global_preferences->make_preference<RosterConfig>(preference_key_);
  if (!preference_.load(&roster_)) {
    FixtureConfig legacy;
    auto legacy_preference = global_preferences->make_preference<FixtureConfig>(preference_key_);
    if (legacy_preference.load(&legacy) && valid_config(legacy)) {
      roster_ = migrate(legacy);
      // Leave the v1 record intact until the first explicit save, preserving rollback.
      message_ = "Your existing light was loaded. You can add more lights below.";
    } else
      roster_ = {};
  }
  if (!valid_roster(roster_, capacity_)) {
    storage_locked_ = true;
    roster_ = {};
    message_ = "Saved light list could not be loaded. No lights are connected. Check capacity/settings before saving.";
  }
  controller_->clear_records();
  for (size_t i = 0; i < capacity_; ++i) {
    const auto &record = roster_.lights[i];
    if (!record.uid)
      continue;
    controller_->set_record(i, record.uid, record.name, address(record.fixture), record.fixture.lighting_id,
                            record.unit);
  }
  if (light_count(roster_) && message_ == "Enter details or start discovery.")
    message_ = "Saved lights loaded. Connecting to saved lights.";
  else if (!light_count(roster_) && message_ == "Enter details or start discovery.")
    message_ = "No saved lights. Add a light manually or use the guide.";
  // Four random 32-bit words become a per-boot 128-bit form token. It supplements
  // HTTP authentication; it is not the lighting ID or Home Assistant API key.
  char nonce[33];
  snprintf(nonce, sizeof(nonce), "%08x%08x%08x%08x", unsigned(esp_random()), unsigned(esp_random()),
           unsigned(esp_random()), unsigned(esp_random()));
  token_ = nonce;
  auto *server = web_server_base::global_web_server_base;
  server->init();
  server->add_handler(this);
  publish_();
}
void OdelicSetup::pause_control_() { controller_->pause(); }
bool OdelicSetup::canHandle(AsyncWebServerRequest *request) const {
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(buffer);
  return url == "/" || url == "/odelic/setup" || url == "/odelic/setup/state" || url == "/odelic/setup/action";
}
void OdelicSetup::reply_(AsyncWebServerRequest *request, int code, const char *type, const std::string &body) {
  auto *response = request->beginResponse(code, type, body);
  response->addHeader("Cache-Control", "no-store");
  response->addHeader("X-Content-Type-Options", "nosniff");
  response->addHeader("X-Frame-Options", "DENY");
  request->send(response);
}
void OdelicSetup::handleRequest(AsyncWebServerRequest *request) {
  if (!request->authenticate(user_.c_str(), password_.c_str())) {
    request->requestAuthentication();
    return;
  }
  char buffer[AsyncWebServerRequest::URL_BUF_SIZE];
  auto url = request->url_to(buffer);
  if (request->method() == HTTP_GET && (url == "/" || url == "/odelic/setup")) {
    reply_(request, 200, "text/html; charset=utf-8", PAGE);
    return;
  }
  if (request->method() == HTTP_GET && url == "/odelic/setup/state") {
    std::string snapshot;
    {
      std::lock_guard<std::mutex> lock(snapshot_mutex_);
      snapshot = snapshot_;
    }
    reply_(request, 200, "application/json", snapshot);
    return;
  }
  if (request->method() != HTTP_POST || url != "/odelic/setup/action") {
    reply_(request, 405, "text/plain", "Unsupported method");
    return;
  }
  if (request->contentLength() > MAX_FORM_BODY_BYTES) {
    reply_(request, 413, "text/plain", "Request too large");
    return;
  }
  auto param = [request](const char *name) -> std::string {
    auto *field = request->getParam(name);
    return field == nullptr ? "" : field->value();
  };
  if (param("token") != token_) {
    reply_(request, 403, "text/plain", "Reload the setup page and retry");
    return;
  }
  std::string action = param("action"), mac = param("mac"), id = param("lighting_id"), name = param("name");
  auto number = [](const std::string &text, uint32_t &value) {
    if (text.empty() || text.size() > 10)
      return false;
    uint64_t parsed = 0;
    for (char c : text) {
      if (c < '0' || c > '9')
        return false;
      parsed = parsed * 10 + c - '0';
    }
    value = parsed;
    return parsed <= UINT32_MAX;
  };
  uint32_t uid = 0, revision = 0;
  if ((action == "save" || action == "remove") &&
      (!number(param("uid"), uid) || !number(param("revision"), revision))) {
    reply_(request, 400, "text/plain", "Reload the light list before editing");
    return;
  }
  uint64_t parsed;
  if (action == "save" &&
      (param("confirmed") != "yes" || !parse_mac(mac, parsed) || (!id.empty() && !valid_id(id)) || !valid_name(name))) {
    reply_(request, 400, "text/plain", "Confirm ownership and provide a valid MAC and eight-digit ID");
    return;
  }
  if (action != "save" && action != "remove" && action != "start" && action != "off" && action != "restored" &&
      action != "cancel") {
    reply_(request, 400, "text/plain", "Unknown setup action");
    return;
  }
  // HTTP callbacks run on another task; all scanner, NVS and hub operations run in the main loop.
  if (action_pending_.exchange(true)) {
    reply_(request, 409, "text/plain", "Another setup request is pending; retry shortly");
    return;
  }
  this->defer("odelic_setup_action", [this, action, mac, id, name, uid, revision]() {
    action_(action, mac, id, name, uid, revision);
    action_pending_ = false;
  });
  reply_(request, 202, "text/plain", "Accepted; check setup status");
}
void OdelicSetup::publish_() {
  std::string result = "{\"token\":" + json_string(token_) + ",\"message\":" + json_string(message_) +
                       ",\"wizard\":" + (wizard_ ? "true" : "false") +
                       ",\"busy\":" + (capture_ != Capture::IDLE || restarting_ ? "true" : "false") +
                       ",\"phase\":" + std::to_string(phase_) + ",\"locked\":" + (storage_locked_ ? "true" : "false") +
                       ",\"capacity\":" + std::to_string(capacity_) +
                       ",\"revision\":" + std::to_string(roster_.revision) + ",\"lights\":[";
  bool first = true;
  for (size_t i = 0; i < capacity_; ++i) {
    const auto &record = roster_.lights[i];
    if (!record.uid)
      continue;
    if (!first)
      result += ',';
    first = false;
    result += "{\"uid\":" + std::to_string(record.uid) + ",\"name\":" + json_string(record.name) +
              ",\"mac\":" + json_string(format_mac(address(record.fixture))) + '}';
  }
  result += "],\"candidates\":[";
  for (size_t i = 0; i < candidates_.size(); ++i) {
    if (i)
      result += ',';
    const auto &c = candidates_[i];
    result += "{\"mac\":" + json_string(format_mac(c.mac)) + ",\"name\":" + json_string(c.name) +
              ",\"rssi\":" + std::to_string(c.rssi) + '}';
  }
  result += "]}";
  std::lock_guard<std::mutex> lock(snapshot_mutex_);
  snapshot_ = result;
}
void OdelicSetup::cancel_(const std::string &message) {
  capture_ = Capture::IDLE;
  wizard_ = false;
  tracker_->stop_scan();
  tracker_->set_scan_continuous(true);
  controller_->resume();
  message_ = message;
  publish_();
}
void OdelicSetup::save_roster_(const RosterConfig &next) {
  if (!preference_.save(&next) || !global_preferences->sync()) {
    message_ = "Could not save the light list. Please retry; active settings were not changed.";
    publish_();
    return;
  }
  pause_control_();
  roster_ = next;
  restarting_ = true;
  message_ = "Light list saved. Restarting bridge; wait and reload this page.";
  publish_();
  this->set_timeout("setup_reboot", SAVE_REBOOT_DELAY_MS, []() { App.safe_reboot(); });
}
void OdelicSetup::action_(const std::string &action, const std::string &mac, const std::string &id,
                          const std::string &name, uint32_t uid, uint32_t revision) {
  if (restarting_)
    return;
  if (action == "cancel") {
    cancel_("Guide cancelled. Restore wall power ON. Saved lights are unchanged.");
    return;
  }
  if (capture_ != Capture::IDLE)
    return;
  if (action == "save" || action == "remove") {
    if (wizard_)
      return;
    if (storage_locked_) {
      message_ = "Saved list is incompatible or damaged. Restore the previous firmware capacity before editing; "
                 "storage has not been overwritten.";
      publish_();
      return;
    }
    if (revision != roster_.revision) {
      message_ = "The light list changed. Reload the page before saving.";
      publish_();
      return;
    }
    // Validate edits on a copy. Rejected/stale/duplicate edits leave the live
    // roster and its Home Assistant device pointers untouched until saving succeeds.
    auto next = roster_;
    if (action == "remove") {
      if (!remove_light(next, uid)) {
        message_ = "That light no longer exists.";
        publish_();
        return;
      }
    } else {
      std::string error = upsert(next, uid, name, mac, id, capacity_);
      if (!error.empty()) {
        message_ = error;
        publish_();
        return;
      }
    }
    save_roster_(next);
    return;
  }
  if (action == "start") {
    pause_control_();
    wizard_ = true;
    phase_ = 0;
    wizard_at_ = millis();
    scans_ = {};
    candidates_.clear();
    begin_phase_();
    return;
  }
  if (wizard_ && ((action == "off" && phase_ == 1) || (action == "restored" && phase_ == 2)))
    begin_phase_();
}
void OdelicSetup::begin_phase_() {
  sample_ = 0;
  capture_ = Capture::STOPPING;
  // Own complete scan windows rather than mixing advertisements seen before
  // and after a user's wall-switch transition. Other lights stay untouched.
  tracker_->set_scan_continuous(false);
  tracker_->stop_scan();
  step_at_ = millis();
  message_ = "Scanning twice (about 10 seconds). Leave the wall switch unchanged.";
  publish_();
}
bool OdelicSetup::parse_device(const esp32_ble_tracker::ESPBTDevice &device) {
  if (capture_ != Capture::RUNNING)
    return false;
  ++advertisements_;
  uint64_t mac = device.address_uint64();
  std::string name = device.get_name();
  if (!valid_utf8(name))
    name = "(unnamed)";
  if (!name_hint(mac, name))
    return false;
  for (auto &candidate : current_) {
    if (candidate.mac == mac) {
      candidate.rssi = device.get_rssi();
      return true;
    }
  }
  if (current_.size() >= MAX_DISCOVERY_CANDIDATES) {
    overflow_ = true;
    return true;
  }
  current_.push_back({mac, name.substr(0, MAX_ADVERTISED_NAME_BYTES), device.get_rssi()});
  return true;
}
void OdelicSetup::on_scan_end() {
  // OTA, cancellation or scanner failure can also end a scan. Such a partial
  // window is not evidence that the wall-powered fixture disappeared.
  if (capture_ != Capture::RUNNING)
    return;
  interrupted_ = !tracker_->scan_running() || millis() - scan_at_ < MIN_SCAN_WINDOW_MS;
  capture_ = Capture::FINISHED;
}
void OdelicSetup::finish_scan_() {
  capture_ = Capture::IDLE;
  if (interrupted_ || !advertisements_ || overflow_) {
    cancel_("Scan inconclusive (interrupted, no advertisements or too many candidates). Restore wall power ON and "
            "retry closer to the fixture.");
    return;
  }
  scans_[phase_ * SCANS_PER_PHASE + sample_] = current_;
  if (++sample_ < SCANS_PER_PHASE) {
    capture_ = Capture::STOPPING;
    step_at_ = millis();
    return;
  }
  ++phase_;
  if (phase_ == 1)
    message_ = "ON scans complete. Turn only the target wall switch OFF, wait, then capture OFF.";
  else if (phase_ == 2)
    message_ = "OFF scans complete. Turn the target wall switch ON, wait, then capture restored ON.";
  else {
    candidates_ = compare(scans_);
    std::string message =
        candidates_.size() == 1
            ? "One consistent candidate found. Confirm ownership, use its MAC and enter the lighting ID to save."
            : "Inconclusive: no unique candidate. Restore wall power ON, move closer and repeat. No fixture was "
              "selected.";
    cancel_(message);
    return;
  }
  publish_();
}
void OdelicSetup::loop() {
  if (wizard_ && millis() - wizard_at_ > WIZARD_TIMEOUT_MS) {
    cancel_("Guide timed out. Restore wall power ON. Saved settings are unchanged.");
    return;
  }
  if (capture_ == Capture::FINISHED) {
    finish_scan_();
    return;
  }
  if (capture_ == Capture::IDLE) {
    return;
  }
  if (millis() - step_at_ > SCAN_LIFECYCLE_TIMEOUT_MS) {
    cancel_("Scanner timed out. Restore wall power ON and retry; no fixture was selected.");
    return;
  }
  if (capture_ == Capture::STOPPING && tracker_->get_scanner_state() == esp32_ble_tracker::ScannerState::IDLE &&
      controller_->transport_idle()) {
    current_.clear();
    advertisements_ = 0;
    overflow_ = false;
    tracker_->set_scan_active(false);
    tracker_->set_scan_duration(SCAN_DURATION_SECONDS);
    tracker_->start_scan();
    capture_ = Capture::STARTING;
  } else if (capture_ == Capture::STARTING && tracker_->scan_running()) {
    scan_at_ = millis();
    capture_ = Capture::RUNNING;
  }
}
}  // namespace esphome::odelic_setup
