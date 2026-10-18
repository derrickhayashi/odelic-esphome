# Repository responsibilities

Agreed on 2026-10-18. The project has three repositories:

| Repository | Purpose | Where Odelic control runs |
|---|---|---|
| `odelic-research` | Protocol research, diagnostic firmware, experiments, and hardware evidence | Atom running the diagnostic firmware |
| `odelic-esphome` | Reusable ESPHome external component, example YAML, and tests | Atom running ESPHome |
| `odelic-home-assistant` (design only) | Server-side Odelic integration and its protocol implementation/tests | Home Assistant, using Bluetooth infrastructure such as an active ESPHome proxy |

The two integration implementations provide different deployment options. The
ESPHome component exposes entities through the native API; the planned Home
Assistant integration would own the protocol on the server. A standard proxy
would transport Bluetooth operations for that server-side approach. Its handshake,
notifications, and control still need proving through the proxy.

The research repository remains available for debugging either implementation.
Record the source research commit and protocol evidence when porting fixes; there
is no shared library or submodule requirement for the first implementation.

Keep one active test controller per fixture. Separate native API probes issued
competing test commands during the first ESPHome validation, so each test session
must close its probe before handing over control.

This repository covers the ESPHome implementation. The server-side Home
Assistant repository currently contains design documentation only and has its own
implementation roadmap.

## Private GitHub origins

- Research: https://github.com/derrickhayashi/odelic-research
- ESPHome: https://github.com/derrickhayashi/odelic-esphome
- Home Assistant: https://github.com/derrickhayashi/odelic-home-assistant

All three repositories are private. Their Git histories and pushes are
independent; the server-side Home Assistant implementation remains planned.
