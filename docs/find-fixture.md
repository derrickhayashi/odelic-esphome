# Find your lighting ID and fixture MAC

Everyone needs the lighting ID from the ConnectedLighting app. If you use the
[runtime setup](runtime-setup.md), its on-device setup page discovers the
fixture MAC using guided wall-switch scans; you do not need an Android phone or
a separate Bluetooth scanner.

Continue to the manual MAC-scanning sections below only for static setup when
you do not already know the fixture MAC. Runtime MAC discovery is implemented,
but its hardware validation is still pending.

Static setup requires both values below. Runtime setup requires the lighting ID
and discovers the MAC during on-device setup:

| Setting | Meaning | Example (dummy) |
|---|---|---|
| `odelic_lighting_id` | Eight-digit ConnectedLighting network login ID | `"00010007"` |
| `odelic_light_mac` | Bluetooth address of the particular fixture | `"10:20:30:40:50:60"` |

Neither is the Home Assistant API encryption key. The MAC must belong to the
light, not the remote, phone, or Atom. Multiple fixtures can share a lighting ID;
the MAC selects the individual fixture. A Bluetooth service UUID and an iPhone
peripheral identifier are not MAC addresses.

## 1. Read the existing lighting ID

1. Open the official ConnectedLighting app that already controls your light.
2. Verify you have selected the intended lighting setup and can control it.
3. Tap the dash at the bottom of the screen to open the menu, then select
   **ID**. The app will show the lighting ID.
4. Record all eight digits, including leading zeroes. In Device Builder's Secrets
   editor, save them as a quoted string:

   ```yaml
   odelic_lighting_id: "00010007"
   ```

The eight-digit ID supplied from the user's app worked in our hardware tests.
If your app version does not show the dash or **ID** menu item, use the
instructions for your installed app from
[Odelic's Connected Lighting support](https://www.odelic.co.jp/products/connectedlighting/).
Do not create, change, or reset the ID to discover the existing one. BLE scanning
alone does not retrieve the eight-digit login ID. An ID printed on a controller
is useful only if it matches the setup currently registered to the light.

After recording the ID, disconnect and fully close ConnectedLighting so it does
not occupy the fixture's BLE connection during subsequent setup.

## 2. Scan for the fixture MAC for static setup

### With an Android phone

1. Install **nRF Connect for Mobile** from Nordic Semiconductor, open its scanner,
   and grant the requested Bluetooth/Nearby devices permissions. Depending on
   Android version, scanning may also require location permission/services.
2. Stand near the light with its wall power ON and start scanning. Do not connect
   to candidate devices or write any values.
3. Record candidate Bluetooth addresses shown in the scanner. A MAC has six
   hexadecimal pairs, such as `10:20:30:40:50:60`. Our fixtures advertised a name
   resembling `PLTCE0C-05`, but names can differ and are shared by other fixtures.
4. Confirm ownership using the wall-power check below before configuring a MAC.

See [Nordic's Android scanner documentation](https://github.com/nordicsemi/Android-nRF-Connect/blob/main/documentation/README.md).

### With an iPhone only

iOS BLE scanners normally show an Apple peripheral identifier instead of the
Bluetooth MAC; do not paste that identifier into `odelic_light_mac`. See
[Nordic's explanation](https://devzone.nordicsemi.com/f/nordic-q-a/74770/nrf-connect-ios).
Use an ESP32 scanner instead if no Android phone is available.

An ESPHome ESP32 scanner can report nearby Bluetooth addresses in its logs. On a
spare ESP32, create a basic Device Builder configuration for its actual board,
with network/logging enabled, and add:

```yaml
esp32_ble_tracker:
```

Install that scanner configuration and open **Logs**. Newly discovered devices
are logged with their addresses; restart the scanner to get a fresh scan for
each comparison if needed. Follow the
[official ESPHome MAC discovery instructions](https://esphome.io/components/esp32_ble_tracker/).
Do not add a duplicate tracker section to an existing configuration. Flashing a
scanner configuration replaces the board's current firmware: use a spare board,
or back up and deliberately restore the working bridge configuration afterward.
Our research firmware is another developer tool, not a requirement for users.

## 3. Confirm which address belongs to your light

The advertised name or strongest signal alone is not enough, especially in an
apartment building with identical fixtures. Use a fresh scan at each stage:

1. With the target fixture's wall switch ON, note the candidate addresses.
2. Turn only that fixture's wall switch OFF. Leave other fixtures unchanged.
3. Wait for fresh scan results. Find which candidate stops advertising; cached
   entries may remain visible, so check whether they are still receiving updates.
4. Turn the wall switch ON again and confirm the same MAC returns.
5. If the result is ambiguous or the address changes, stop and investigate rather
   than guessing or connecting to a neighbour's fixture.

Use one ordinary OFF/ON cycle with time to scan between changes, not rapid
repeated power cycling that could trigger a pairing/reset sequence. The remote's
OFF button is insufficient: the fixture remains electrically powered and can
continue advertising. Restore wall power ON when finished.

This disappearance/return procedure identified our second fixture in the research
session. Record each confirmed MAC with a room label, then save yours in Secrets:

```yaml
odelic_light_mac: "10:20:30:40:50:60"
odelic_lighting_id: "00010007"
```

Return to the [Device Builder installation guide](device-builder.md#2-add-your-fixture-settings).
The production component resolves the fixture's changing mesh address itself;
you do not need to discover or configure that address.
