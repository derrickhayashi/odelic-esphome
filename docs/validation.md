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

## Not yet tested on hardware

- RC911- and ConnectedLighting-driven state updates.
- Fixture wall-power loss and recovery.
- Encrypted wireless OTA uploads.
- Runtime setup authentication, discovery, NVS persistence/migration, and fixture
  login/control.
- Multiple-fixture BLE rotation, command isolation, and larger-roster reliability.
- Home Assistant creation, renaming, and removal of runtime fixture sub-devices.
- Live remote-source downloads and the Device Builder browser installation flow.

Eight fixtures is a storage/build limit; hardware control has been verified with
one static fixture.
