#include "odelic_setup.h"
#include <cassert>
#include <memory>
using namespace esphome;
using namespace esphome::odelic_setup;
class Setup : public OdelicSetup {
 public:
  odelic_controller::OdelicController *controller{nullptr};
  void action(const std::string &name, const std::string &mac = "", const std::string &id = "",
              const std::string &label = "Living room", uint32_t uid = 0, uint32_t revision = 0) {
    action_(name, mac, id, label, uid, revision);
  }
  const std::string &snapshot() const { return snapshot_; }
  const std::string &token() const { return token_; }
  const RosterConfig &roster() const { return roster_; }
  size_t phase() const { return phase_; }
  bool wizard() const { return wizard_; }
  size_t candidates() const { return candidates_.size(); }
  void light_command(size_t slot, bool on, float brightness) { controller->light_command(slot, on, brightness); }
  void preset_command(size_t slot, size_t preset) { controller->preset_command(slot, preset); }
  void loop() override {
    OdelicSetup::loop();
    controller->loop();
  }
};
struct Fixture {
  std::array<odelic::OdelicHub, MAX_LIGHTS> hubs;
  odelic::OdelicHub &hub = hubs[0];
  esp32_ble_tracker::ESP32BLETracker tracker;
  odelic_controller::OdelicController controller;
  std::array<std::unique_ptr<odelic::OdelicLight>, MAX_LIGHTS> outputs;
  std::array<std::unique_ptr<odelic_controller::RosterLightState>, MAX_LIGHTS> states;
  std::array<std::unique_ptr<odelic_controller::RosterPreset>, MAX_LIGHTS> presets;
  std::array<std::unique_ptr<odelic_controller::RosterValid>, MAX_LIGHTS> valid;
  Setup setup;
  explicit Fixture(size_t capacity = MAX_LIGHTS) {
    App.devices.clear();
    controller.set_capacity(capacity);
    setup.set_capacity(capacity);
    setup.controller = &controller;
    setup.set_controller(&controller);
    setup.set_tracker(&tracker);
    setup.set_credentials("setup", "synthetic-password");
    for (size_t i = 0; i < MAX_LIGHTS; ++i) {
      outputs[i] = std::make_unique<odelic::OdelicLight>();
      states[i] = std::make_unique<odelic_controller::RosterLightState>(outputs[i].get());
      presets[i] = std::make_unique<odelic_controller::RosterPreset>();
      valid[i] = std::make_unique<odelic_controller::RosterValid>();
      controller.set_slot_hub(i, &hubs[i]);
      controller.set_slot(i, outputs[i].get(), states[i].get(), presets[i].get(), valid[i].get());
    }
    setup.setup();
    controller.setup();
  }
  void window(bool target, bool neighbour = true, bool interrupted = false) {
    setup.loop();
    setup.loop();
    if (target)
      setup.parse_device({0x102030405060ULL, "PLTCEOC-05", -80});
    if (neighbour)
      setup.parse_device({0xecc57f010203ULL, "PLTCEOC-05", -40});
    clock_ms += interrupted ? 1000 : 5000;
    if (interrupted)
      tracker.state = esp32_ble_tracker::ScannerState::STOPPING;
    setup.on_scan_end();
    tracker.state = esp32_ble_tracker::ScannerState::IDLE;
    setup.loop();
  }
};
void persist_roster(const RosterConfig &roster) {
  auto bytes = reinterpret_cast<const uint8_t *>(&roster);
  persisted.assign(bytes, bytes + sizeof(roster));
}
int main() {
  persisted.clear();
  Fixture f;
  assert(f.hub.suspended && f.hub.client.mac == 0);
  AsyncWebServerRequest unauthorized;
  unauthorized.path = "/odelic/setup/state";
  unauthorized.authorized = false;
  f.setup.handleRequest(&unauthorized);
  assert(unauthorized.challenged && unauthorized.body.empty());
  AsyncWebServerRequest csrf;
  csrf.path = "/odelic/setup/action";
  csrf.verb = HTTP_POST;
  f.setup.handleRequest(&csrf);
  assert(csrf.status == 403);
  AsyncWebServerRequest save;
  save.path = "/odelic/setup/action";
  save.verb = HTTP_POST;
  save.params = {{"token", {f.setup.token()}},
                 {"action", {"save"}},
                 {"confirmed", {"yes"}},
                 {"uid", {"0"}},
                 {"revision", {"0"}},
                 {"name", {"Living room"}},
                 {"mac", {"10:20:30:40:50:60"}},
                 {"lighting_id", {"00010007"}}};
  f.setup.handleRequest(&save);
  assert(save.status == 202 && persisted.empty());
  f.setup.drain();
  assert(!persisted.empty());
  assert(f.setup.snapshot().find("00010007") == std::string::npos);
  assert(f.setup.timeout);
  f.setup.timeout();
  assert(App.reboots == 1);
  Fixture restored;
  assert(restored.hub.suspended);
  restored.setup.loop();
  assert(!restored.hub.suspended && restored.hub.client.mac == 0x102030405060ULL);
  assert(restored.hub.id == "00010007");
  auto saved = persisted;
  restored.setup.action("start");
  assert(restored.hub.suspended);
  restored.setup.light_command(0, false, 1);  // ignored during discovery
  restored.window(true);
  restored.window(true);
  assert(restored.setup.phase() == 1);
  restored.setup.action("off");
  restored.window(false);
  restored.window(false);
  assert(restored.setup.phase() == 2);
  restored.setup.action("restored");
  restored.window(true);
  restored.window(true);
  assert(!restored.setup.wizard() && restored.setup.candidates() == 1 && persisted == saved);
  restored.setup.loop();
  assert(!restored.hub.suspended && restored.hub.commands.empty());
  restored.setup.action("start");
  restored.window(true, true, true);
  assert(!restored.setup.wizard() && persisted == saved);
  restored.setup.action("start");
  clock_ms += 300001;
  restored.setup.loop();
  assert(!restored.setup.wizard());
  restored.setup.action("start");
  restored.setup.action("cancel");
  assert(!restored.setup.wizard() && persisted == saved);
  storage_ok = false;
  restored.setup.action("save", "10:20:30:40:50:62", "00010009", "Second", 0, 1);
  assert(persisted == saved);
  storage_ok = true;
  // Independent transports: an unreachable first light never gates the other lights.
  RosterConfig list;
  assert(upsert(list, 0, "A", "10:20:30:40:50:60", "00010007", 8).empty());
  assert(upsert(list, 0, "B", "10:20:30:40:50:62", "00010008", 8).empty());
  assert(upsert(list, 0, "C", "10:20:30:40:50:64", "00010009", 8).empty());
  persist_roster(list);
  saved = persisted;
  Fixture too_small(1);
  too_small.setup.action("save", "10:20:30:40:50:80", "00010007", "Do not overwrite", 0, 0);
  assert(persisted == saved && light_count(too_small.setup.roster()) == 0);
  Fixture multi;
  assert(App.devices.size() == 3 && App.devices[0]->get_device_id() == 1 && App.devices[2]->get_device_id() == 3);
  multi.setup.loop();
  for (size_t i = 0; i < 3; ++i)
    assert(!multi.hubs[i].suspended);
  for (size_t i = 3; i < MAX_LIGHTS; ++i)
    assert(multi.hubs[i].suspended);
  assert(multi.hubs[1].client.mac == 0x102030405062ULL && multi.hubs[2].id == "00010009");
  multi.setup.light_command(2, false, 1);
  multi.setup.light_command(1, true, 0.5);
  multi.hubs[1].confirm();
  multi.setup.loop();
  assert(multi.hubs[0].commands.empty() && multi.hubs[2].commands.empty());
  assert(multi.valid[1]->state && !multi.valid[2]->state);
  assert(multi.hubs[1].commands.size() == 1 && multi.hubs[1].commands[0].brightness == 0.5);
  multi.hubs[2].confirm();
  multi.setup.loop();
  assert(multi.hubs[2].commands.size() == 1 && multi.hubs[2].commands[0].mac == 0x102030405064ULL &&
         !multi.hubs[2].commands[0].on);
  multi.hubs[1].busy = true;
  multi.setup.preset_command(1, 1);
  multi.setup.preset_command(2, 2);
  multi.setup.loop();
  assert(multi.hubs[1].commands.size() == 1);
  assert(multi.hubs[2].commands.size() == 2 && multi.hubs[2].commands.back().preset == 2);
  multi.hubs[1].busy = false;
  multi.setup.loop();
  assert(multi.hubs[1].commands.size() == 2 && multi.hubs[1].commands.back().preset == 1);
  // A healthy session survives the former visit budget; only its own disconnection
  // invalidates its diagnostic. Other connected sessions remain ready.
  clock_ms += 20001;
  multi.setup.loop();
  assert(!multi.hubs[1].suspended && !multi.hubs[2].suspended && multi.valid[2]->state);
  multi.hubs[1].status = {};
  multi.setup.loop();
  assert(!multi.valid[1]->state && multi.valid[2]->state);
  multi.hubs[1].confirm();
  multi.setup.loop();
  assert(multi.valid[1]->state);
  clock_ms += 30001;
  multi.setup.loop();
  assert(!multi.valid[1]->state && !multi.valid[2]->state);
  // Discovery must wait until every asynchronous disconnect has completed.
  multi.hubs[2].client.connection = esp32_ble_tracker::ClientState::ESTABLISHED;
  multi.controller.pause();
  assert(!multi.controller.transport_idle());
  multi.hubs[2].client.connection = esp32_ble_tracker::ClientState::IDLE;
  assert(multi.controller.transport_idle());
  multi.controller.resume();
  multi.setup.loop();
  // Stale revision and duplicate MAC never change persistent storage.
  saved = persisted;
  multi.setup.action("remove", "", "", "", 1, 0);
  assert(persisted == saved);
  multi.setup.action("save", "10:20:30:40:50:62", "00010007", "duplicate", 0, list.revision);
  assert(persisted == saved);
  // Removing a light preserves the rest of the list; new records get a new UID.
  multi.setup.action("remove", "", "", "", 2, list.revision);
  Fixture after_remove;
  assert(light_count(after_remove.setup.roster()) == 2 && find_uid(after_remove.setup.roster(), 2) < 0);
  assert(after_remove.setup.roster().lights[0].uid == 1 && after_remove.setup.roster().lights[2].uid == 3);
  after_remove.setup.action("save", "10:20:30:40:50:66", "00010010", "D", 0, list.revision + 1);
  Fixture after_add;
  assert(after_add.setup.roster().lights[1].uid == 4);
  assert(after_add.setup.snapshot().find("00010010") == std::string::npos);
  // Renaming keeps device identity and the existing ID; it does not repurpose another light.
  after_add.setup.action("save", "10:20:30:40:50:66", "", "Renamed D", 4, list.revision + 2);
  Fixture renamed;
  assert(renamed.setup.roster().lights[1].uid == 4 &&
         std::string(renamed.setup.roster().lights[1].fixture.lighting_id) == "00010010");
  // A queued operation expires rather than actuating much later.
  renamed.setup.light_command(0, false, 1);
  clock_ms += 60001;
  renamed.setup.loop();
  renamed.hub.confirm();
  renamed.setup.loop();
  assert(renamed.hub.commands.empty());
  renamed.setup.preset_command(2, 1);
  renamed.setup.action("start");
  renamed.setup.action("cancel");
  renamed.setup.loop();
  renamed.hub.confirm();
  renamed.setup.loop();
  assert(renamed.hub.commands.empty());
  // Capacity remains eight: every fixture has an independent session, pending
  // intent and validity. Confirming one MAC cannot release another MAC's queue.
  RosterConfig eight;
  for (unsigned i = 0; i < MAX_LIGHTS; ++i) {
    char mac[18];
    snprintf(mac, sizeof(mac), "10:20:30:40:50:%02x", 0x60 + i);
    assert(upsert(eight, 0, "Fixture", mac, "00010007", 8).empty());
  }
  persist_roster(eight);
  Fixture all;
  all.setup.loop();
  for (size_t i = 0; i < MAX_LIGHTS; ++i) {
    assert(!all.hubs[i].suspended && all.hubs[i].client.mac == 0x102030405060ULL + i);
    all.setup.light_command(i, false, 1);
  }
  for (size_t i = 0; i < MAX_LIGHTS; ++i) {
    all.hubs[i].confirm();
    all.setup.loop();
    assert(all.hubs[i].commands.size() == 1 && !all.hubs[i].commands[0].on);
    for (size_t j = i + 1; j < MAX_LIGHTS; ++j)
      assert(all.hubs[j].commands.empty());
  }
  // Pause cancels every queue and hub transaction, and resume does not replay.
  for (size_t i = 0; i < MAX_LIGHTS; ++i) {
    all.setup.preset_command(i, 1);
    all.hubs[i].busy = true;
  }
  all.controller.pause();
  all.controller.resume();
  all.setup.loop();
  for (size_t i = 0; i < MAX_LIGHTS; ++i) {
    assert(!all.hubs[i].busy && !all.valid[i]->state);
    all.hubs[i].confirm();
  }
  all.setup.loop();
  for (size_t i = 0; i < MAX_LIGHTS; ++i)
    assert(all.hubs[i].commands.size() == 1);
  // The old single-fixture blob migrates without overwriting it until an explicit save.
  FixtureConfig legacy;
  make_config("10:20:30:40:50:60", "00010007", legacy);
  auto bytes = reinterpret_cast<const uint8_t *>(&legacy);
  persisted.assign(bytes, bytes + sizeof(legacy));
  saved = persisted;
  Fixture migrated;
  assert(light_count(migrated.setup.roster()) == 1 && persisted == saved);
  persisted.assign(sizeof(RosterConfig), 255);
  Fixture corrupt;
  assert(corrupt.hub.suspended && light_count(corrupt.setup.roster()) == 0);
}
