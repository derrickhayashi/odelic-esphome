# Install with ESPHome Device Builder

This is the recommended first-install workflow for a confirmed fixture on an
AtomS3 Lite. Device Builder is also used to install the optional
[runtime fixture setup](runtime-setup.md), but you should follow that guide
instead if you need runtime fixture management.
Use ESPHome 2026.9.1 for the currently tested version. Configuration validation
and compilation have been tested separately from the Device Builder UI; see
[validation](validation.md). The source repository is public; package imports
must reference a branch or revision containing the selected files. Live remote
imports and the Device Builder browser workflow still need validation.

## 1. Open Device Builder

Choose either the Home Assistant app or the standalone Python installation below.
Both provide Device Builder's browser interface.

### Home Assistant OS

1. Open **Settings → Apps → Install app** (called **Add-ons → Add-on Store**
   in older versions).
2. Find **ESPHome Device Builder** and select **Install**.
3. Select **Start**, then **Open Web UI**.

### Standalone Python installation (macOS/Linux)

You can run Device Builder on your computer even if Home Assistant runs on a
separate machine. Use Python 3.12–3.14 for this project's pinned ESPHome version.
Check your Python version:

```sh
python3 --version
```

If you need Python on macOS, Homebrew can install a supported version:

```sh
brew install python@3.13
```

When using that version, replace `python3` with `python3.13` in the first command
below. Create an isolated environment, install Device Builder with its ESPHome
compiler dependency, and keep ESPHome pinned to the tested version:

```sh
python3 -m venv ~/.venvs/esphome-builder
source ~/.venvs/esphome-builder/bin/activate
python -m pip install "esphome-device-builder[esphome]" "esphome==2026.9.1"
mkdir -p ~/esphome-config
esphome-device-builder ~/esphome-config
```

Open **http://localhost:6052** in your browser on that computer. The
`~/esphome-config` directory stores your device YAML and secrets; keep it between
sessions. Leave the terminal running while using Device Builder. To stop it,
press **Ctrl+C** in that terminal.

To start it again later, open a terminal and run:

```sh
source ~/.venvs/esphome-builder/bin/activate
esphome-device-builder ~/esphome-config
```

Homebrew's `brew install esphome` provides the ESPHome command-line tool; the
Python package above supplies the separate Device Builder web interface. A
standalone desktop app is another option in the
[official installation guide](https://esphome.io/install/). The Python workflow
here follows its [Device Builder instructions](https://esphome.io/install/#device-builder-web-ui),
with ESPHome pinned for this project. The standalone installation commands have
not been tested in a fresh environment for this guide.

### Create or import the bridge configuration

For a new bridge, select **New Device**, give it a name, and enter your 2.4 GHz
Wi-Fi details. Select ESP32-S3 when choosing the device family. Finish creating
the configuration; installation can wait until the Odelic configuration is added.
Open **Edit** on its card. Keep the generated `esphome`, `wifi`, `api`, and `ota`
sections, including the generated API encryption key. The package below supplies
the exact board settings, so remove the wizard's top-level `esp32` section before
adding it. Keep only one definition of each top-level YAML section.

**For our already-flashed Atom:** import its existing device YAML and private
secrets instead of generating a replacement identity. Preserve `odelic-bridge`,
and its existing API key. The development files are
`examples/atoms3-lite.yaml` and ignored `examples/secrets.yaml`; adapt the local
package/component references using the fragment below. Do not copy the
command-line example unchanged into Device Builder: its relative paths refer to
this checkout. Back up the working configuration before replacing it.

## 2. Add your fixture settings

To enter these settings on the Atom itself or use guided MAC discovery, use
[Runtime fixture setup](runtime-setup.md) instead of the static fragment below.


First follow [Find your lighting ID and fixture MAC](find-fixture.md) for each
light whose values you do not already have.

Open Device Builder's **Secrets** editor. Source downloads from the public
repository require no GitHub account, username, or access token.

Add these entries to Device Builder's secrets file, replacing the dummy values:

```yaml
odelic_light_mac: "10:20:30:40:50:60"
odelic_lighting_id: "00010007"
```

Use your confirmed fixture MAC and existing eight-digit ConnectedLighting ID.
Keep the ID quoted, including any leading zeroes. The lighting ID authenticates
to the lighting network; the API key connects Home Assistant to the Atom. They
are separate credentials. Keep the secrets file private; only the component and
package source are public.

## 3. Add the Odelic package

Copy [this configuration fragment](../examples/device-builder.yaml) into the
wizard-created device YAML, keeping its existing network and API sections.
If it already has `substitutions`, `packages`, or `external_components`, merge
entries into those sections rather than adding duplicate section names.
Save, then choose **Validate** from the device card's menu.

The package adds power/brightness, the four white presets, and a state-valid
diagnostic. It leaves Wi-Fi, device name, API encryption, and encrypted OTA
configuration to your local configuration. Optional one-light substitutions are
`odelic_light_name` and `odelic_unit` (default `0`).

For multiple statically configured lights, keep the package's first fixture and
append additional records from the device YAML. The first fixture continues to
use `odelic_light_mac`, `odelic_lighting_id`, and `odelic_light_name`:

```yaml
odelic_controller:
  fixtures:
    - id: bedroom
      name: Bedroom
      mac_address: !secret bedroom_odelic_mac
      lighting_id: !secret bedroom_odelic_id
```

Each `id` must be unique and should remain unchanged: it determines the stable
Home Assistant sub-device identity. The display `name` can change without
changing that identity. MAC addresses must also be unique. Static and runtime
rosters use the same independent per-fixture BLE sessions, bounded queues,
fresh-read-before-write rule, entities and state-valid behavior. Only their
configuration source differs. Healthy fixture connections stay open while other
fixtures reconnect. The packages reserve eight BLE connection slots; keep
`esp32_ble.max_connections` at least as large as the controller's reserved
`max_lights` (and account for any other BLE clients). The existing eight-fixture
capacity and identities are unchanged. See [concurrent BLE sessions](concurrent-ble.md).

The concurrent firmware passed two-light power, brightness, preset and isolation
checks with physical confirmation. First-state readback had a 1.20-second median
in the short acceptance test. Offline recovery and larger-roster reliability
remain untested. See [validation](validation.md).

The package keeps its original first light on the bridge device with its existing
name, preserving the installed one-light layout. Additional records are exposed
as separate sub-devices. Do not add `root_device: true` to another fixture.

Both Git references currently use `main`. They should use the same revision;
for a reproducible deployment, replace both with the same tested commit SHA.


## 4. Install on the Atom

Close other serial monitors and test probes. Keep the fixture's wall power ON,
and fully close ConnectedLighting. Plug the Atom into the computer using a USB
data cable. Select **Install** in Device Builder. If the browser offers **Plug
into this computer**, use a supported Chromium browser over HTTPS or localhost
and select the Atom's USB port. If that option is unavailable, select **Manual
download**, choose the factory image for first USB installation, then use
[ESPHome Web](https://web.esphome.io/) in a supported browser to connect to the
Atom and install the downloaded image. Keep downloaded firmware private because
it contains device credentials. Later updates can use **Wirelessly** once the
Atom is reachable on Wi-Fi; ESPHome authenticates and encrypts OTA with the
device's API encryption key.

If an existing bridge currently uses only an OTA password, install this release
once over USB. After that installation, remove the obsolete `ota_password`
secret and use encrypted wireless OTA with the existing API key.

Flashing replaces the Atom's current firmware. Returning to the research firmware
requires uploading both research firmware and its LittleFS configuration.

## 5. Add the bridge to Home Assistant

Once the bridge joins Wi-Fi, open **Settings → Devices & services** in Home
Assistant. Configure the discovered ESPHome bridge. If discovery does not appear,
select **Add integration → ESPHome** and enter its IP address (port `6053`).
Discovery uses mDNS and generally needs a network that allows multicast.

If Home Assistant asks for an encryption key, copy the value from this device's
`api → encryption → key` entry, or its referenced secret, in Device Builder.
Do not generate another key at this step. Some versions can transfer the key
automatically; this guide does not depend on that feature.

Check for **Ceiling light**, **White preset**, and **Fixture state valid** (which
may appear among diagnostic entities). A valid state query is required before
trusting the displayed fixture state. Record the baseline, test power,
brightness, and presets one at a time, then restore the baseline. Successful
installation is separate from Home Assistant UI and physical control validation.

## Official references

- [Getting started with ESPHome](https://esphome.io/install/getting-started/)
- [Packages](https://esphome.io/components/packages/)
- [External components](https://esphome.io/components/external_components/)
- [Native API encryption](https://esphome.io/components/api/)
