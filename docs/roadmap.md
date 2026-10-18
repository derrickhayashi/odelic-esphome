# ESPHome integration roadmap

This roadmap lists remaining work only. Implemented capabilities and completed
checks are recorded in [validation](validation.md).

## Complete single-light acceptance

- Confirm that changes made with the RC911 remote and ConnectedLighting app are
  reflected in Home Assistant.
- Validate recovery after fixture wall-power loss and bridge reconnection,
  including state validity and command replay safeguards.

## Validate runtime setup and multiple lights

- Exercise guided MAC discovery, saved configuration, restart, and fixture login
  on the AtomS3 Lite.
- Confirm Home Assistant sub-device creation, rename, and removal for runtime
  fixtures.
- Test BLE rotation and command isolation with at least two physical fixtures,
  including power, brightness, presets, offline fixtures, and state freshness.
- Characterize responsiveness and reliability with larger saved rosters.
