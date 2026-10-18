#pragma once
// Minimal platform boundaries for running the real setup controller on the host.
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../../components/odelic/protocol.h"

namespace esphome {
inline uint32_t clock_ms = 0;
inline uint32_t millis() { return clock_ms; }
namespace setup_priority { inline constexpr float LATE = -100; }
class Component {
 public:
  virtual void setup() {}
  virtual void loop() {}
  virtual float get_setup_priority() const { return 0; }
  void defer(const char *, std::function<void()> f) { deferred.push_back(f); }
  void set_timeout(const char *, uint32_t, std::function<void()> f) { timeout = f; }
  void drain() { auto work = deferred; deferred.clear(); for (auto &f : work) f(); }
  std::vector<std::function<void()>> deferred;
  std::function<void()> timeout;
};
inline std::vector<uint8_t> persisted;
inline bool storage_ok = true;
class ESPPreferenceObject {
 public:
  template<typename T> bool load(T *data) { if (persisted.size() != sizeof(T)) return false; memcpy(data, persisted.data(), sizeof(T)); return true; }
  template<typename T> bool save(const T *data) { if (!storage_ok) return false; auto bytes = reinterpret_cast<const uint8_t *>(data); persisted.assign(bytes, bytes + sizeof(T)); return true; }
};
struct Preferences {
  template<typename T> ESPPreferenceObject make_preference(uint32_t) { return {}; }
  bool sync() { return storage_ok; }
};
inline Preferences preferences;
inline Preferences *global_preferences = &preferences;
struct Device {
  uint32_t id{0}; const char *name{nullptr};
  void set_device_id(uint32_t value) { id = value; }
  uint32_t get_device_id() const { return id; }
  void set_name(const char *value) { name = value; }
};
constexpr int ENTITY_CATEGORY_DIAGNOSTIC = 2;
constexpr int ENTITY_FIELD_ENTITY_CATEGORY_SHIFT = 26;
struct Entity {
 protected:
  Device *device_{nullptr};
};
namespace light {
struct LightOutput {};
struct LightState : public Component, public Entity {
  explicit LightState(LightOutput *) {}
};
}
namespace binary_sensor {
struct BinarySensor : public Entity {
  bool state{false};
  void publish_state(bool value) { state = value; }
};
}
struct Application {
  int reboots = 0;
  std::vector<Device *> devices;
  void safe_reboot() { ++reboots; }
  void register_device(Device *device) { devices.push_back(device); }
  template<typename T> void register_light(T *, const char *, uint32_t, uint32_t) {}
  template<typename T> void register_select(T *, const char *, uint32_t, uint32_t) {}
  template<typename T> void register_binary_sensor(T *, const char *, uint32_t, uint32_t) {}
};
inline Application App;
namespace esp32_ble_tracker {
enum class ScannerState { IDLE, STARTING, RUNNING, STOPPING };
enum class ClientState { IDLE, ESTABLISHED };
struct ESPBTDevice {
  uint64_t mac; std::string name; int rssi;
  uint64_t address_uint64() const { return mac; }
  std::string get_name() const { return name; }
  int get_rssi() const { return rssi; }
};
struct ESPBTDeviceListener {
  virtual bool parse_device(const ESPBTDevice &) = 0;
  virtual void on_scan_end() {}
};
struct ESP32BLETracker {
  ScannerState state = ScannerState::IDLE;
  bool continuous = true, active = false;
  int starts = 0;
  void start_scan() { state = ScannerState::RUNNING; ++starts; }
  void stop_scan() { state = ScannerState::IDLE; }
  void set_scan_continuous(bool value) { continuous = value; }
  void set_scan_active(bool value) { active = value; }
  void set_scan_duration(uint32_t) {}
  bool scan_running() const { return state == ScannerState::RUNNING; }
  ScannerState get_scanner_state() const { return state; }
};
}
namespace odelic {
struct Client {
  uint64_t mac = 0;
  esp32_ble_tracker::ClientState connection = esp32_ble_tracker::ClientState::IDLE;
  void set_address(uint64_t value) { mac = value; }
  auto state() const { return connection; }
};
struct FixtureCommandSink {
  virtual void light_command(size_t, bool, float) = 0;
  virtual void preset_command(size_t, size_t) = 0;
};
struct OdelicHub;
struct OdelicLight : public light::LightOutput {
  void set_parent(OdelicHub *) {}
  void set_command_sink(FixtureCommandSink *, size_t) {}
};
struct OdelicPreset : public Entity {
  void set_parent(OdelicHub *) {}
  void set_command_sink(FixtureCommandSink *, size_t) {}
};
struct OdelicHub {
  bool suspended = false, busy = false;
  uint32_t revision = 0;
  protocol::Status status{};
  Client client;
  std::string id;
  struct Command { uint64_t mac; std::string id; bool on; float brightness; int preset; };
  std::vector<Command> commands;
  void suspend_for_setup() { suspended = true; busy = false; status = {}; }
  void resume_after_setup() { suspended = false; }
  void set_lighting_id(const std::string &value) { id = value; }
  void set_unit(uint8_t) {}
  void set_light(OdelicLight *) {}
  void set_preset(OdelicPreset *) {}
  void set_state_valid(binary_sensor::BinarySensor *) {}
  Client *parent() { return &client; }
  const protocol::Status &actual() const { return status; }
  uint32_t state_revision() const { return revision; }
  bool ready() const { return !suspended && status.known; }
  bool pending() const { return busy; }
  void request_light(bool on, float brightness) { commands.push_back({client.mac, id, on, brightness, -1}); }
  void request_preset(size_t preset) { commands.push_back({client.mac, id, false, 0, int(preset)}); }
  void confirm() { status = {true, true, true, 4, 4}; ++revision; }
};
}
}

enum http_method { HTTP_GET, HTTP_POST };
struct AsyncWebParameter { std::string text; const std::string &value() const { return text; } };
struct AsyncWebServerResponse {
  int code; std::string body;
  std::map<std::string, std::string> headers;
  void addHeader(const char *key, const char *value) { headers[key] = value; }
};
struct AsyncWebServerRequest {
  static constexpr size_t URL_BUF_SIZE = 128;
  std::string path, body;
  std::map<std::string, AsyncWebParameter> params;
  http_method verb = HTTP_GET;
  int status = 0;
  bool authorized = true, challenged = false;
  AsyncWebServerResponse *response = nullptr;
  ~AsyncWebServerRequest() { delete response; }
  template<typename T> std::string url_to(T &) const { return path; }
  bool authenticate(const char *, const char *) const { return authorized; }
  void requestAuthentication() { challenged = true; }
  http_method method() const { return verb; }
  size_t contentLength() const { return 100; }
  AsyncWebParameter *getParam(const char *key) { auto item = params.find(key); return item == params.end() ? nullptr : &item->second; }
  AsyncWebServerResponse *beginResponse(int code, const char *, const std::string &value) { response = new AsyncWebServerResponse{code, value, {}}; return response; }
  void send(AsyncWebServerResponse *value) { status = value->code; body = value->body; }
};
struct AsyncWebHandler {
  virtual bool canHandle(AsyncWebServerRequest *) const = 0;
  virtual void handleRequest(AsyncWebServerRequest *) = 0;
};
namespace esphome::web_server_base {
struct WebServerBase { void init() {} void add_handler(AsyncWebHandler *) {} };
inline WebServerBase server;
inline WebServerBase *global_web_server_base = &server;
}
