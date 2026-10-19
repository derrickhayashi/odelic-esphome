# Validation

## Configuration and builds

- Static and runtime configurations validated with ESPHome 2026.9.1.
- Static one-light and eight-slot runtime firmware compiled for the M5Stack
  AtomS3 Lite with ESP32-S3 / ESP-IDF 5.5.5.
- Native API and OTA encryption configuration validated using the same API key.

## Automated tests

- Protocol and command planning: unicast packets, fixture identity filtering,
  brightness quantization, colour/brightness preservation, and OFF/unknown guards.
- Schema and package loading: lighting ID validation and leading zeroes,
  static/runtime rosters, capacity limits, distinct device IDs, and preservation
  of Device Builder identity, Wi-Fi, API, and OTA settings. Remote source loading
  used mocked Git downloads.
- Roster storage and migration: duplicate fixtures, capacity limits, UTF-8 names,
  stable identities, rename/removal, v1 migration, and corrupt or incompatible
  records.
- Shared controller: authentication and form-token checks, deferred saves,
  simulated restart, discovery sequencing/cancellation/timeouts, storage failures,
  isolated per-light commands, queue expiry, and offline-fixture fairness.
- Setup page browser tests: manual entry, ownership confirmation, discovery,
  ambiguous results, add/edit/remove, capacity, ID privacy, inert advertised names,
  and phone-width layout, using synthetic HTTP responses.
- Hardware acceptance script: entity selection, fresh-readback requirements,
  rejection of invalid state, and sequence/restoration using a fake API client.
- C++ protocol, model, and controller host tests passed with address and
  undefined-behaviour sanitizers.

## Concurrent-session implementation (2026-10-19)

The current controller preserves the eight-fixture capacity and reserves a
separate persistent BLE client/protocol hub per fixture slot. The two-fixture
static build has now been uploaded and physically tested as described below;
eight simultaneous fixtures remain untested.

- Protocol, roster-model and controller host tests passed with address and
  undefined-behaviour sanitizers. Controller checks include eight independent
  sessions, per-MAC queued-command release, busy/offline-session independence,
  stale-state invalidation, and all-session pause/cancellation.
- All 16 Python configuration/acceptance tests passed, including rejection of
  insufficient BLE stack capacity and the unsupported nine-slot S3 configuration.
- Runtime eight-slot and static two-fixture configurations compiled with the
  pinned ESPHome 2026.9.1 / ESP-IDF 5.5.5 toolchain and synthetic credentials.
- Generated runtime code contains eight tracker clients and eight distinct hubs.
  S3 controller activity capacity is ten, Bluedroid ACL capacity nine, and the
  GATT application table ten (including the two internal applications).
- The eight-slot runtime build reports 139,379 bytes static RAM usage (40.8%).
  This does not measure heap consumption with eight live connections.
- C++ formatting and Python Ruff checks passed for the changed controller/tests.

### Concurrent two-fixture hardware acceptance

The uncommitted implementation based on `6ae62a7` was USB-flashed and tested on
2026-10-19 (device build 21:50:22 +0900; configuration hash `0x727dc3c7`).
Private evidence includes the exact tested component/package diff.

- Encrypted native API verified the installed build and both entity identities.
  Startup preserved both pre-flash fixture baselines.
- USB diagnostics showed two active BLE links and MAC-labelled login/readback
  from both fixtures. No BLE disconnect events occurred during the short test;
  each fixture logged in once during the capture.
- Both fixtures passed OFF/ON, 50% brightness preserving colour, and all four
  presets preserving brightness. Every step checked fresh readback from both
  fixtures and confirmed that the other fixture stayed unchanged.
- The user physically confirmed isolation for both sequences and reported that
  the second fixture felt noticeably faster.
- Across 13 state-changing control operations, first-target-readback latency was
  1.053–1.457 seconds, median 1.196 seconds. The earlier rotating capture's same
  operations measured 2–9 seconds, median 6 seconds (one-second timestamp
  resolution). No-op presets and restoration steps are excluded. This is a short
  before/after comparison, not a controlled performance benchmark. Full isolation
  confirmation additionally waits for the other fixture's next polling update.
- Both baselines were restored: main ON at 75%, Natural white; second ON at 100%,
  Warm white. Home Assistant live context also showed both restored power and
  brightness values. The API test client and passive USB capture were closed.

The concurrent firmware remains installed. Pending: second-fixture Home Assistant
browser UI controls, runtime setup, offline recovery while another session stays
connected, and larger-roster soak/memory testing. See
[concurrent BLE sessions](concurrent-ble.md).

## Hardware and Home Assistant

Tested with one statically configured fixture on an M5Stack AtomS3 Lite:

- USB firmware upload, Wi-Fi connection/recovery, fixture login, and MAC-matched
  state queries.
- Startup preserved fixture power, colour, and brightness.
- Encrypted native API connection and enumeration of the light, white preset,
  and fixture-state-valid entities.
- Home Assistant UI integration and control of the static fixture.
- OFF/ON, brightness changes, and all four white presets, with separate fixture
  readbacks and physical confirmation. Brightness changes preserved colour;
  preset changes preserved brightness.
- The automated native API acceptance sequence verified controls and restored
  the captured baseline using confirmed fixture readbacks.

## Two-fixture static hardware acceptance (2026-10-19)

Tested the shared controller with two owned fixtures on the same AtomS3 Lite,
using source revision `6ae62a7`, ESPHome 2026.9.1, and a static two-record roster:

- Configuration validation, compilation and USB upload passed. Protocol and
  controller host tests and all 14 Python tests passed.
- Encrypted native API exposed separate power/brightness, preset and state-valid
  entities for both fixtures. The first retained its existing root-device identity;
  the second used a separate sub-device identity.
- BLE rotation authenticated and queried both selected fixture MACs. Each passed
  OFF/ON, 50% brightness preserving colour, and all four presets preserving
  brightness, with actual fixture readback after each operation.
- Each command required fresh light and preset updates for both fixtures with valid
  diagnostics; the non-target fixture retained its preceding power, brightness
  and preset. The user physically confirmed isolation for both sequences.
- Both captured baselines were restored: first ON at 75%, Natural white; second
  ON at 100%, Warm white. The test API client disconnected afterward.

This validates a short two-light static sequence. It does not validate runtime
setup, the second fixture's Home Assistant UI, offline-fixture recovery, larger
rosters, or long-duration reliability. Private captures and the installed device
configuration remain in ignored local directories.

## Not yet tested on hardware

- RC911- and ConnectedLighting-driven state updates.
- Fixture wall-power loss and recovery.
- Encrypted wireless OTA uploads.
- Runtime setup authentication, discovery, NVS persistence/migration, and fixture
  login/control.
- Offline-fixture behavior, larger-roster responsiveness and long-duration reliability.
- Home Assistant creation, renaming, and removal of runtime fixture sub-devices.
- Live remote-source downloads and the Device Builder browser installation flow.

Eight fixtures is a storage/build limit; hardware control has been verified with
two static fixtures.
