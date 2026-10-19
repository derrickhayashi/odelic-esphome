# Concurrent BLE sessions

The controller retains the existing eight-fixture roster/storage limit. Every
reserved slot now owns a BLE client and Odelic protocol hub. Configured fixtures
keep their connections instead of alternating through one transport; unused slots
remain disabled with no target. Startup and reconnection only authenticate/query,
without actuation. Each session resolves its own confirmed fixture MAC to its
current mesh address and retains separate login keys, state and transaction data.

Static configurations reserve one session per fixture (or explicit `max_lights`).
Runtime setup reserves its build-time `max_lights`, default eight. The explicitly
configured seed hub/client provides slot zero for YAML compatibility. The controller
generates the remaining clients/hubs, inheriting the seed query interval (default
five seconds). First-light entity IDs, runtime UIDs, the version-2 NVS roster and
v1 migration stay unchanged; no storage conversion is required.

## Stack capacity

Both packages set `esp32_ble.max_connections: 8`. It is configurable, but must
cover all reserved fixture slots and any other BLE clients. Validation rejects
insufficient capacity instead of silently falling back to rotation. A smaller
runtime build should lower `odelic_max_lights` and `esp32_ble.max_connections`
together. Static builds can lower the BLE limit to their reserved fixture count.

For eight slots on the pinned ESPHome 2026.9.1 / ESP-IDF 5.5.5 ESP32-S3 build:

- ESPHome reserves eight client connections and registers eight tracker clients.
- Bluedroid `CONFIG_BT_ACL_CONNECTIONS=9` includes ESPHome's scan allowance.
- `CONFIG_BT_CTRL_BLE_MAX_ACT=10` covers connections plus scanning/advertising
  controller activities.
- The global `GATT_MAX_APPS=10` build flag increases Bluedroid's application table:
  the upstream default is eight entries, two of which are internal GAP/GATT apps.
  Without this adjustment eight independently registered clients cannot all fit.

The codegen uses the configured BLE connection limit when sizing these resources.
See the [ESPHome BLE configuration](https://esphome.io/components/esp32_ble/),
[ESP-IDF 5.5.5 S3 connection limits](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/api-reference/kconfig-reference.html#config-bt-acl-connections)
and [pinned Bluedroid application-table definition](https://github.com/espressif/esp-idf/blob/v5.5.5/components/bt/host/bluedroid/common/include/common/bt_target.h).
ESP32-S3 has a shared radio for Wi-Fi/BLE and finite internal RAM. Reserving eight
slots does not establish eight-fixture runtime reliability. The implementation
must be compiled and then independently hardware-tested at the intended load.

## Lifecycle and commands

Every fixture retains its own coalescing queue with a 60-second expiry. A request
is dispatched only after that session has valid readback, and the hub forces a
fresh MAC-matched query before writing. Failed/disconnected hub transactions are
cleared rather than replayed. Offline and busy sessions do not gate other hub
loops. Actual state is invalidated on disconnect/failure and after 30 seconds
without confirmed readback; unknown state never means OFF.

Discovery and roster saves pause every client, clear all queues and hub intents,
and invalidate all fixtures. Discovery waits for every transport to become idle
before scanning; cancel/resume only restarts the configured sessions. Authenticated
HTTP callbacks, stable fixture identities and storage behavior are unchanged.

## Evidence

The static build passed a two-fixture concurrent hardware test: both connections
stayed open, power/brightness/presets and isolation passed with physical confirmation,
and both baselines were restored. First-target-readback median was 1.196 seconds
across 13 state-changing commands, versus about six seconds in the prior rotating
capture. The full isolation check additionally waits for both fixtures' fresh
polling updates. See [validation](validation.md) for the comparison's scope.

Eight reserved sessions compile and pass host checks, but eight live fixtures,
runtime setup, offline recovery and long-duration reliability remain untested.
