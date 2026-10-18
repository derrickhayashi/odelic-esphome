# Odelic ESPHome Bridge

An unofficial, independent project for Odelic lighting.

Control compatible Odelic Connected Lighting fixtures from Home Assistant with
an M5Stack AtomS3 Lite running ESPHome. The bridge communicates with the light
over Bluetooth and exposes the controls through Home Assistant's native API.

## Hardware support

The supported and tested hardware is the **M5Stack AtomS3 Lite**, using an
ESP32-S3 with the ESP-IDF framework. The included Device Builder packages are
configured for that board.

Other ESP32 boards may work if they support ESPHome's `ble_client` and
`esp32_ble_tracker` components with ESP-IDF, but they are not currently
validated or supported by this project. Using another board requires a custom
configuration and may require changing its board, flash, logging, and USB
settings. Do not assume that a successful compilation proves that the board
can communicate reliably with a fixture.

## What you can do

Each configured light provides:

- Turn the light on and off.
- Set brightness in 5% steps.
- Select Warm white, Natural white, Cool white, or the Warm/Cool endpoint.
- See the light's actual state after commands and when it changes through the
  Odelic remote or app.

Brightness changes keep the light's current colour. White preset changes keep
the light's current brightness.

The bridge does not currently provide RGB colours, a calibrated colour
temperature slider, effects, or smooth fades. Leave Home Assistant transitions
disabled.

## New user? Start here

For the first installation, use the **static setup** unless you specifically
need to add or change lights from the Atom after installation.

**Device Builder is the installation tool for both setups.** The choice below
decides which Odelic configuration you install into Device Builder:

- Choose **static setup** for one light, or for a fixed list of lights that you
  are comfortable editing in YAML.
- Choose **runtime setup** when you want a local setup page, guided fixture
  discovery, or the ability to add, rename, edit, and remove lights without
  rebuilding the firmware.

If you are unsure, follow [Static setup: one light](#static-setup-one-light).
You can add the runtime setup later by rebuilding with its configuration, but
the two setups must not be installed together.

### Prerequisites

Both setup options require:

- An M5Stack AtomS3 Lite connected to the Atom by USB for the initial
  installation.
- An Odelic fixture that is already paired with ConnectedLighting.
- The fixture's eight-digit ConnectedLighting lighting ID.
- A 2.4 GHz Wi-Fi network and a Home Assistant installation on the same
  network.

The lighting ID is not the same as the Home Assistant API encryption key or the
fixture's Bluetooth MAC address. Keep it private.

### Find the lighting ID

Open the official ConnectedLighting app that already controls the fixture.
Select the intended lighting setup, tap the dash at the bottom of the screen
to open the menu, and select **ID**. The app will show the lighting ID. Record
all eight digits, including leading zeroes. If your app version differs, use
[Odelic's ConnectedLighting
support](https://www.odelic.co.jp/products/connectedlighting/).

Do not create or reset an ID to discover it. Bluetooth scanning cannot retrieve
the lighting ID. You need this existing ID for both setup options.

The **static setup also requires the fixture's Bluetooth MAC address** before
installation. If you do not know the MAC address, use the [runtime fixture
setup](docs/runtime-setup.md), which can guide you through discovering it with
the fixture's wall switch. You still need to provide the lighting ID when
saving the discovered fixture. Runtime setup does not require an Android phone
or a separate Bluetooth scanner.

## Setup options

### Static setup: one light

This is the recommended starting point when you already know the fixture's MAC
address and lighting ID. They are entered once in Device Builder's secrets. The
static setup is physically
validated for one fixture. It can also be extended to a fixed list of lights,
but those additional lights have not had physical hardware validation.

1. Find the fixture's lighting ID and, for static setup, its Bluetooth MAC
   address using [Find your lighting ID and MAC](docs/find-fixture.md).
2. Follow the [Device Builder installation guide](docs/device-builder.md) to
   install the bridge on an AtomS3 Lite.
3. Add the bridge to Home Assistant using the device's native API.

Keep your Wi-Fi credentials, Home Assistant API encryption key, and lighting ID
in your local configuration. OTA uses the same API key for encryption; do not
publish it.

### Runtime setup: up to eight lights

The [runtime fixture setup](docs/runtime-setup.md) is an alternative firmware
configuration, not a step you do in addition to Device Builder. You still use
Device Builder to install it, then use the Atom's password-protected local
setup page to enter, discover, rename, edit, and remove lights. It supports a
saved roster of 1–8 fixtures and gives each light its own Home Assistant
sub-device.

The runtime setup, storage, and build checks are passing. Multi-light hardware
testing and Home Assistant acceptance are still pending, so treat this path as
experimental.

## Important limitations

- Only one fixture connection is active at a time. With several lights,
  readback and commands can take longer.
- The bridge reports confirmed fixture state; a command acknowledgment alone is
  not treated as proof that the light changed.
- The Odelic app or another controller should not be connected at the same
  time as this bridge.
- This project is experimental and is not an official Odelic or ESPHome
  integration.

For implementation status and test evidence, see
[validation](docs/validation.md).

## Development

Development uses ESPHome 2026.9.1, ESP-IDF, Python 3.12–3.14, ESPHome's BLE
client, and asynchronous GATT events. See the
[development workflow](docs/development.md) for local builds and tests.
