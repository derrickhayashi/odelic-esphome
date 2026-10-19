# Development

- This is the ESPHome implementation; keep the sibling odelic-research repository
  available for protocol debugging. Do not replace its diagnostic workflow.
- Use the pinned ESPHome version and uv.lock. Build, flash, native API checks, and
  physical fixture confirmation are separate validation stages.
- Never commit real credentials, captures, decompiled APK code, or generated builds.
- Keep BLE operations non-blocking. Match the fixture MAC before accepting its
  virtual address and state; acknowledgments are not actual state.
- Preserve actual brightness for presets and actual colour for brightness changes.
- Do not claim upstream acceptance or hardware validation from a successful build.
- ESPHome is pinned to 2026.9.1 with ESP-IDF; use Python 3.12–3.14 and `uv.lock`.
- Run protocol and host tests with:
  `clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Icomponents/odelic tests/protocol_test.cpp -o /tmp/esphome-odelic-test`,
  `/tmp/esphome-odelic-test`, and
  `uv run --locked python -m unittest discover -s tests -p 'test_*.py'`.
- Keep build, flash, native API checks, and physical fixture confirmation as
  separate validation stages.

# Related repositories

- Research/debugging repository: `odelic-research` (separate checkout);
  private GitHub origin: https://github.com/derrickhayashi/odelic-research
- This ESPHome repository: `odelic-esphome`;
  public GitHub origin: https://github.com/derrickhayashi/odelic-esphome
- Server-side Home Assistant integration: `odelic-home-assistant` (design only);
  private GitHub origin: https://github.com/derrickhayashi/odelic-home-assistant

The three Git repositories have independent status and history. Explicitly
select the working directory and read that repository's agent instructions before
editing or pushing. A push here does not update the research repository. Keep
the research and Home Assistant repositories private. The user confirmed that
this ESPHome repository should remain public on 2026-10-19; do not change any
repository visibility without an explicit request. See `docs/repositories.md` for
the agreed roles and `docs/validation.md` for tested capabilities and pending work.
Never assume the Atom still has the same firmware or that another chat has closed
its test probe.

# Installation workflow

Use `docs/device-builder.md` as the primary user installation path. The reusable
`packages/atoms3-lite.yaml` holds hardware and fixture controls; device identity,
Wi-Fi, and API encryption stay in the user's local YAML/secrets. Use the API key
for encrypted OTA; do not add a separate OTA password.
Preserve existing names and keys when migrating an installed bridge. Keep the
CLI example for development. Remote package and component refs must match.
The user-facing installation workflow uses public source access and requires no
GitHub token. Keep credentials, captures and generated firmware out of the public
repository.

# Runtime setup

`odelic_setup` is an optional authenticated local page with saved fixture settings
and passive wall-switch discovery. See `docs/runtime-setup.md`. The static package
remains supported. Runtime builds require auto_connect false in YAML; only valid
saved configuration enables the client after boot. Discovery suspends the hub,
clears intents and never connects to candidates. Keep HTTP callbacks limited to
validation/snapshots and deferred main-loop actions. Do not expose stored lighting
IDs or API keys in setup responses/logs. Hardware validation remains separate from
model/schema/browser tests and compilation. No generic firmware release exists.

Runtime setup now stores a version-2 roster of up to eight fixtures. Keep persisted
UIDs stable across editing and never reuse a removed identity. Entities are
registered before API enumeration only for configured records, as sub-devices
whose names may change independently of identity. Runtime codegen reserves entity
capacity; do not mix static entities/devices into that package. Each reserved slot
has its own BLE client and protocol hub; keep healthy connections open and require
fresh MAC-matched readback before commands. The seed `odelic_id`/BLE client supplies
the first session, and codegen creates the remaining sessions. BLE slot accounting,
S3 controller activity capacity and Bluedroid GATT application capacity must cover
all sessions. Keep per-light queues bounded/expiring and ensure offline lights and
busy sliders cannot starve other polling. Discovery and roster saves clear every
queue. Preserve v1 migration and lock incompatible lists rather than overwriting
them. See `docs/runtime-setup.md` for restart, downgrade, capacity and Home
Assistant registry limitations.

# Connection capacity and hardware evidence

- The roster/build ceiling remains eight fixtures. Both packages reserve eight
  BLE stack connection slots by default; `esp32_ble.max_connections` must cover
  the controller's reserved `max_lights` plus any other BLE clients. Validate
  capacity rather than silently falling back to single-connection rotation.
- Each configured fixture keeps an independent persistent BLE session. Preserve
  separate authentication keys, MAC-matched mesh addresses, actual state and
  pending transactions. Never reuse one fixture's session data for another.
- Two simultaneous connections passed physical power, brightness, preset,
  isolation and baseline-restoration tests on 2026-10-19. Eight-session builds and
  host tests are not proof of eight live fixtures or long-duration reliability.
- First-fixture Home Assistant UI control was previously verified. Both fixtures
  appeared in Home Assistant live context, but second-fixture browser UI controls
  remain untested. Runtime setup, remote/app-driven feedback and offline recovery
  also require hardware acceptance. Consult `docs/concurrent-ble.md` and
  `docs/validation.md` before updating tested-capability claims.
- A recorded installed build is only a last-known observation. Verify current
  firmware and controller ownership before using USB commands, the native API or
  flashing. Keep machine-specific connection details in ignored local notes.

# Machine-local working context

Keep committed documentation portable: use repository-relative paths, repository
names/URLs or documented placeholders. Keep personal checkout/tool paths, serial
ports and temporary working locations in ignored `local/workspace.md`. Read that
optional file when present, and verify its observations before use. It cannot
override authorization or repository rules. Keep credentials in the existing
ignored secrets files and reference them without copying their values into notes.

# Code clarity and comments

- Follow ESPHome's upstream code conventions for every component change. Format
  C++ headers and sources with the checked-in `.clang-format` (derived from
  ESPHome's `dev` configuration); use two-space indentation and keep normal code
  lines to 120 columns or fewer. Use `lower_snake_case` names, `UpperCamelCase`
  type names, `UPPER_SNAKE_CASE` constants, trailing underscores on fields, and
  `this->` for instance fields and methods. Format Python as Ruff would and
  follow ESPHome's schema/codegen patterns.
- Run the available formatter and lint checks before committing. If an upstream
  tool is unavailable locally, report that limitation rather than substituting
  an arbitrary version or skipping the check silently.
- Explain application-specific logic where it lives: Odelic packet fields and
  byte ordering, login construction, identity filtering, command/readback rules,
  scheduling, discovery, persistence and stable Home Assistant identity.
- Replace non-obvious numeric literals with descriptive constants. Include units
  in timing/size names and explain whether values come from protocol evidence,
  storage compatibility or chosen application policy. Link relevant research docs
  and label uncertainties; do not invent a protocol meaning for unknown bytes.
- Comments should explain intent, constraints and tradeoffs, not restate obvious
  code. Keep comments current when behaviour changes. Document storage layout
  changes and migration requirements.
- Do not put credentials or personal working paths in comments. Keep local
  environment context in ignored `local/workspace.md`.
