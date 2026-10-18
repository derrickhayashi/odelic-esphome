# Command-line development

Install [uv](https://docs.astral.sh/uv/), then:

```sh
uv sync --locked
cp examples/secrets.example.yaml examples/secrets.yaml
```

Edit the private `examples/secrets.yaml` with your 2.4 GHz Wi-Fi settings, a fresh
API encryption key, your confirmed fixture MAC and eight-digit lighting ID. Keep
the lighting ID quoted to preserve zeroes. Generate an API key with
`openssl rand -base64 32`; the key in the example is public dummy data. The
examples use that same key for encrypted OTA, so do not add an OTA password or
flash with placeholder credentials.
Before uploading or opening USB logs, identify the connected board and set
`SERIAL_PORT` to its actual port. Device Builder can show available serial ports.

```sh
uv run --locked esphome config examples/atoms3-lite.yaml
uv run --locked esphome compile examples/atoms3-lite.yaml
uv run --locked esphome upload examples/atoms3-lite.yaml --device "${SERIAL_PORT}"
uv run --locked esphome logs examples/atoms3-lite.yaml --device "${SERIAL_PORT}"
```

The serial port varies by machine. Close other serial monitors before upload.
Flashing this replaces the diagnostic firmware on the Atom and changes its
partition layout. To return to the research firmware, upload its firmware **and
filesystem configuration** again; the research JSON is not used by this project.

Close ConnectedLighting during the initial test. Boot connects and queries state
without sending power or brightness commands. In Home Assistant, use the ESPHome
integration to add the bridge when discovered, or add its IP address manually,
using the private API key when prompted. A successful compile or API connection
alone does not establish that the fixture can be controlled.

For a repeatable one-fixture acceptance sequence, close other probes and
ConnectedLighting, keep wall power on, and run:

```sh
uv run --locked python scripts/hardware_acceptance.py \
  --host BRIDGE_IP \
  --confirm-actuation
```

The explicit flag is required because the script operates a real fixture. It
captures the confirmed baseline, tests OFF/ON, 50% brightness, and every white
preset using post-command fixture-state predicates rather than fixed delays.
Every preset must preserve 50% brightness. A `finally` path restores the original
power, preset, and brightness and reports restoration failure as a test failure.
The timestamped evidence log is written under ignored `captures/`. The script
requires exactly one light; multi-light scheduling needs a separate fixture-aware
acceptance test. API readback establishes protocol behavior, while physical
observation remains a separate human confirmation.


## Runtime setup checks

The optional setup package is described in [runtime setup](runtime-setup.md).
Its example needs `setup_username` and `setup_password` in private secrets,
in addition to the usual Wi-Fi/API/OTA settings.

```sh
clang++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Icomponents/odelic_setup tests/setup_model_test.cpp -o /tmp/odelic-setup-model-test
/tmp/odelic-setup-model-test
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -Itests/setup_stubs -I. -Icomponents/odelic_setup tests/setup_runtime_test.cpp components/odelic_setup/odelic_setup.cpp components/odelic_controller/odelic_controller.cpp -o /tmp/odelic-setup-runtime-test
/tmp/odelic-setup-runtime-test
uv run --locked python -m unittest discover -s tests -p 'test_*.py'
```

With Node and Playwright/Chromium available, `node tests/setup_page_test.cjs`
exercises the actual firmware HTML against simulated responses. It does not test
the ESP32 HTTP server, authentication implementation, NVS, or BLE. Set
`PLAYWRIGHT_CHROMIUM_EXECUTABLE_PATH` if using an existing compatible browser.
These optional browser checks do not add dependencies to the firmware toolchain.

The controller host test compiles the actual setup implementation against fake
HTTP, preferences, hub and scanner boundaries. It verifies request authentication
and form-token gates, deferred saving, loading a saved ID after simulated restart,
scan sequencing, suspension/resumption, cancellation/timeout and storage failures.
It does not exercise ESP-IDF's implementations of those boundaries.

## Local working notes

Machine-specific checkout paths, tool locations and last-known ports belong in
ignored `local/workspace.md`, not in committed docs. This optional file helps an
agent resume work on a particular machine without prescribing its layout to
other contributors. Keep secret values in their existing ignored secrets files.
