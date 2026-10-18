# Manage lights on the Atom

The optional setup firmware lets you enter a fixture MAC and lighting ID in a
local web page, or discover the MAC using wall-switch scans. The Atom saves those
values across restarts. Add, name, edit, or remove individual lights after
installation without rebuilding firmware. The default capacity is eight lights.
This is an implementation under hardware validation, not a tested public release.

The YAML-configured roster remains supported and now uses the same fixture
controller and scheduler. Use the setup package below only when you want runtime
fixture configuration, persistence, discovery, and editing. It uses one BLE client
and rotates between the saved lights; do not combine it with other BLE clients,
static light/select/sensor definitions, manually defined sub-devices, or a Bluetooth
proxy. Both paths still require the existing eight-digit ID from ConnectedLighting.

## Install

1. Install/open Device Builder using [the installation guide](device-builder.md#1-open-device-builder).
2. Create a device for the AtomS3 Lite, or back up and adapt an existing device's
   configuration. Preserve its name, Wi-Fi, and API encryption key; the package
   uses that key for encrypted OTA.
3. In **Secrets**, add a setup username and a unique password of at least eight
   characters:

   ```yaml
   odelic_setup_username: "setup"
   odelic_setup_password: "REPLACE_WITH_YOUR_OWN_PASSWORD"
   ```

4. Add [the runtime setup fragment](../examples/device-builder-setup.yaml) to your
   device YAML. Replace the existing Odelic package/component entries if you
   already have them; merge other top-level YAML sections rather than duplicating
   keys. Remove the wizard's top-level `esp32` section because the package supplies
   the board. Do not add the static fixture fragment alongside this one.
5. Validate and install. Source references assume the repository is public and
   these files are pushed; use the same revision for package and component.
6. Wait for Wi-Fi, then open `http://YOUR_BRIDGE_IP/odelic/setup` on the same local
   network. The `.local` hostname also works where mDNS is available. Sign in with
   the setup username/password from step 3. These differ from the lighting ID and
   Home Assistant API key.

For development, `examples/atoms3-lite-setup.yaml` uses local component/package
files and the usual private secrets plus `setup_username` and `setup_password`.
The static example is unchanged as a supported configuration path.

The setup page uses local HTTP with password authentication and a per-boot form
token. Use it on a trusted LAN; do not expose it to the Internet. It does not
return the stored lighting ID or API key. Fixture settings are stored in the
ESP32's preferences/NVS, without application-level encryption; protect physical
access and firmware/configuration files as you do other ESPHome credentials.

## Enter details directly

1. Choose **Add a light**, then enter its name, confirmed Bluetooth MAC and
   eight-digit lighting ID. Preserve
   leading zeroes. The page validates the format before saving.
2. Check the ownership confirmation, then choose **Save and restart bridge**.
3. Wait for the restart and reconnect. The bridge uses the saved settings to log
   in and query the fixture. It does not send a power/brightness/preset command
   merely because you saved settings.
4. Home Assistant receives a separate sub-device for each saved light, named after
   its label. Each has **Light** (power/brightness), **White preset**, and diagnostic
   **Fixture state valid** entities. Check the actual readback for each light;
   a successful save alone does not establish that its lighting ID was accepted.
   Before any lights are saved, no fixture entities are exposed.

The saved-lights list has **Edit** and **Remove** actions for each record. A
confirmed removal only stops the bridge managing that light; it does not reset the
fixture, remove its remote pairing, or change the other saved records. Every
add/edit/remove saves the full list and restarts the bridge, briefly interrupting
all its Home Assistant connections. No firmware rebuild or reflash is required.
A blank lighting-ID field when editing keeps that particular light's existing ID. The stored
ID is never filled into the form. Changing the MAC requires confirming ownership
again. Multiple lights may share the same ID; each MAC can appear only once.
Two open setup pages cannot silently overwrite each other's edits: stale list
revisions are rejected and must be reloaded. Erasing the device's NVS/factory-resetting it removes the saved settings.
The runtime package deliberately starts without a target when no valid saved
settings exist; it does not fall back to a previously compiled fixture address.

## Find the MAC with wall-switch scans

1. Fully close ConnectedLighting and stop other test controllers. Stand the Atom
   near the light. Keep every other fixture's wall power unchanged.
2. Leave the target wall switch ON and choose **Start with wall power ON**.
3. Wait for the two fresh five-second scans to finish. When the page asks, turn
   only that wall switch OFF, wait for power to settle, and choose the OFF scan.
4. After those scans finish, restore wall power ON, wait, and capture restored ON.
5. If exactly one MAC consistently disappears and returns, choose **Use this MAC**.
   Confirm it was your fixture whose wall power changed, supply the lighting ID,
   and save. No address is saved or connected merely because it was discovered.
6. If there are zero or multiple matches, repeat closer to the fixture. Never
   choose a neighbour's fixture by strongest signal or shared name alone.

Use wall power, not the remote's OFF button. Do not rapidly cycle power. The
scanner uses advertised names starting with `PLT` or the investigated `EC:C5:7F`
address prefix as hints. Other models may need a different hint filter. Candidates
are compared by MAC, never by name/RSSI. An incomplete, interrupted, empty-radio,
or overflowing scan is inconclusive. Two scans per stage reduce transient misses;
they cannot prove ownership if the wrong circuit was switched or RF losses happen
to mimic power loss.

During the guide the bridge disconnects from its current fixture, clears all
pending light commands and marks every saved fixture's state invalid. Power, brightness and preset requests are
ignored. Cancellation, failure, or the five-minute timeout restores normal query
operation for the previously saved lights, without saving candidates. Always
restore wall power ON yourself. The page does not control your wall switch.

## Capacity, polling and identity

`odelic_max_lights` defaults to `8`; the current supported build range is 1–8.
For example, set `odelic_max_lights: "5"` in the device YAML substitutions before
building to reserve five light slots. The setup page reports capacity and disables
adding when full; editing and removal remain available. Do not reduce the build
capacity below occupied slots. An incompatible stored list is locked against
replacement until you restore the previous capacity/configuration.

Only one BLE fixture connection is active at a time. Normal polling visits the
saved lights in turn; pending user requests receive priority without starving
overdue polling. A connection/query visit is bounded to 20 seconds, so an offline
light cannot block all others indefinitely. A healthy single-light configuration
can stay connected. More lights or weak links mean longer feedback/control delays;
eight is a storage/build limit, not a hardware reliability claim.

Each fixture has its own coalescing pending request. It is applied only after a
fresh MAC-matched query on that fixture's connection. Requests expire after 60
seconds and dispatched requests are never replayed after switching/disconnection.
Starting discovery or saving the roster clears all pending requests. Power,
brightness and presets still use individually addressed protocol messages.

Each state-valid diagnostic records recent actual readback. It can remain valid
between intentional rotation visits; it becomes false when polling times out,
state gets stale, or discovery pauses control. The stale threshold is
`30 seconds + 25 seconds × configured light count`. The light entity retains its
last confirmed values; invalid state is not a confirmed OFF result.

Saved sub-device identities are independent of display names and storage slots.
Renaming keeps the identity; removing a light and adding a replacement allocates
a new identity, so old automations cannot silently take over a different fixture.
Home Assistant entity names that you customized yourself may still need updating
in Home Assistant. Actual registry behaviour needs hardware/UI validation.

The prior single-fixture preferences format is read into the first record as
**Ceiling light**, with the original MAC/ID preserved. Migration leaves the old
blob untouched until an explicit edit/add/remove saves the new list. Once saved
in the multi-light format, older single-light firmware will not read it; back up
the settings before downgrading. The new sub-device layout may leave old single-light
entities to clean up in Home Assistant; automatic registry migration is not promised.

## Scope and remaining work

This implements runtime fixture setup, not a complete generic firmware installer.
Wi-Fi provisioning, setup-page credentials, API encryption and OTA still follow
ESPHome's device configuration. There is no shared prebuilt firmware release or
browser installer yet. Do not install synthetic test binaries on a real device.

Before calling this hardware-tested, check first boot without a fixture,
authenticated page access, manual save/reboot/login, guided ON/OFF/ON identification,
ambiguous results, cancellation/timeout, power/brightness/presets after setup, and
restart persistence. Also verify two-light isolation, add/edit/remove after
installation, per-light presets/brightness, an unreachable fixture, and persistence
of the full roster. Record baseline settings and restore them after tests. See
[validation](validation.md) for the current evidence.
