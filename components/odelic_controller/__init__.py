"""Static/runtime roster with an independent persistent BLE session per fixture."""

import zlib

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import (
    binary_sensor,
    ble_client,
    esp32,
    esp32_ble,
    esp32_ble_tracker,
    light,
    select,
)
from esphome.components.odelic import OdelicHub, lighting_id
from esphome.components.odelic.select import OPTIONS
from esphome.const import CONF_ID, CONF_NAME
from esphome.core import CORE

DEPENDENCIES = ["odelic"]
AUTO_LOAD = ["light", "select", "binary_sensor"]
ns = cg.esphome_ns.namespace("odelic_controller")
OdelicController = ns.class_("OdelicController", cg.Component)
RosterLightState = ns.class_("RosterLightState", light.LightState, cg.Component)
RosterPreset = ns.class_("RosterPreset", select.Select)
RosterValid = ns.class_("RosterValid", binary_sensor.BinarySensor)
OdelicOutput = cg.esphome_ns.namespace("odelic").class_(
    "OdelicLight", light.LightOutput
)
MAX_LIGHTS = 8
# Pinned Bluedroid table layout and S3 controller activity overhead (IDF 5.5.5).
BLUEDROID_DEFAULT_MAX_APPS = 8
BLUEDROID_INTERNAL_APPS = 2
S3_SCAN_ADV_ACTIVITIES = 2


def fixture_key(value):
    return cv.valid_name(value)


FIXTURE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): fixture_key,
        cv.Required(CONF_NAME): cv.All(cv.string_strict, cv.Length(min=1, max=64)),
        cv.Required("mac_address"): cv.mac_address,
        cv.Required("lighting_id"): lighting_id,
        cv.Optional("unit", default=0): cv.int_range(min=0, max=255),
        cv.Optional("root_device", default=False): cv.boolean,
    }
)

SLOT_SCHEMA = cv.Schema(
    {
        cv.GenerateID("hub_id"): cv.declare_id(OdelicHub),
        cv.GenerateID("client_id"): cv.declare_id(ble_client.BLEClient),
        cv.GenerateID("state_id"): cv.declare_id(RosterLightState),
        cv.GenerateID("output_id"): cv.declare_id(OdelicOutput),
        cv.GenerateID("preset_id"): cv.declare_id(RosterPreset),
        cv.GenerateID("valid_id"): cv.declare_id(RosterValid),
    }
).extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)


def expand_slots(config):
    config = dict(config)
    fixtures = config.get("fixtures", [])
    capacity = config.get("max_lights", len(fixtures) or MAX_LIGHTS)
    capacity = cv.int_range(min=1, max=MAX_LIGHTS)(capacity)
    if len(fixtures) > capacity:
        raise cv.Invalid("fixtures exceeds max_lights")
    keys = [item[CONF_ID] for item in fixtures]
    if len(keys) != len(set(keys)):
        raise cv.Invalid("fixture ids must be unique")
    macs = [str(item["mac_address"]) for item in fixtures]
    if len(macs) != len(set(macs)):
        raise cv.Invalid("fixture MAC addresses must be unique")
    if sum(item.get("root_device", False) for item in fixtures) > 1:
        raise cv.Invalid("only one fixture can use the bridge root device")
    uids = [zlib.crc32(key.encode("utf-8")) or 1 for key in keys]
    if len(uids) != len(set(uids)):
        raise cv.Invalid("fixture ids produced a device identity collision")
    config["_uids"] = uids
    config["_slots"] = [{} for _ in range(capacity)]
    config["max_lights"] = capacity
    # The explicitly declared seed client reserves its own slot. Reserve every
    # generated client too so stack capacity checks include the entire roster.
    return esp32_ble.consume_connection_slots(capacity - 1, "odelic_controller")(config)


CONFIG_SCHEMA = cv.All(
    expand_slots,
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OdelicController),
            cv.Required("odelic_id"): cv.use_id(OdelicHub),
            cv.Optional("max_lights"): cv.int_range(min=1, max=MAX_LIGHTS),
            cv.Optional("fixtures", default=[]): cv.ensure_list(FIXTURE_SCHEMA),
            cv.Required("_slots"): cv.ensure_list(SLOT_SCHEMA),
            cv.Required("_uids"): cv.ensure_list(cv.uint32_t),
        }
    ).extend(cv.COMPONENT_SCHEMA),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    hub = await cg.get_variable(config["odelic_id"])
    cg.add(var.set_capacity(len(config["_slots"])))
    cg.add_define("USE_DEVICES")
    cg.add_define("ESPHOME_DEVICE_COUNT", len(config["_slots"]))
    # ESP-IDF's default GATT application table has eight entries, including two
    # internal GAP/GATT registrations. Eight independent clients require ten.
    # Set this globally so Bluedroid and its callers agree on the table layout.
    full = CORE.config
    connections = full["esp32_ble"]["max_connections"]
    seed = next(
        item
        for item in full["odelic"]
        if str(item[CONF_ID]) == str(config["odelic_id"])
    )
    applications = connections + BLUEDROID_INTERNAL_APPS
    if applications > BLUEDROID_DEFAULT_MAX_APPS:
        cg.add_build_flag(f"-DGATT_MAX_APPS={applications}")
    if esp32.get_esp32_variant() == "ESP32S3":
        # S3 controller activity budget includes scanning and advertising, even
        # when the application primarily uses client connections (IDF 5.5.5).
        esp32.add_idf_sdkconfig_option(
            "CONFIG_BT_CTRL_BLE_MAX_ACT", connections + S3_SCAN_ADV_ACTIVITIES
        )
    for index, slot in enumerate(config["_slots"]):
        slot_hub = hub
        if index:
            client = cg.new_Pvariable(slot["client_id"])
            await cg.register_component(client, {})
            await esp32_ble_tracker.register_client(client, slot)
            cg.add(client.set_address(0))
            cg.add(client.set_auto_connect(False))
            slot_hub = cg.new_Pvariable(slot["hub_id"])
            await cg.register_component(
                slot_hub, {"update_interval": seed["update_interval"]}
            )
            cg.add(client.register_ble_node(slot_hub))
        cg.add(var.set_slot_hub(index, slot_hub))
        output = cg.new_Pvariable(slot["output_id"])
        state = cg.new_Pvariable(slot["state_id"], output)
        await cg.register_component(state, {})
        cg.add(state.set_default_transition_length(0))
        cg.add(state.set_gamma_correct(1.0))
        cg.add(state.set_restore_mode(light.RESTORE_MODES["ALWAYS_OFF"]))
        preset = cg.new_Pvariable(slot["preset_id"])
        cg.add(preset.traits.set_options(OPTIONS))
        valid = cg.new_Pvariable(slot["valid_id"])
        cg.add(var.set_slot(index, output, state, preset, valid))
        for platform, entity in [
            ("light", state),
            ("select", preset),
            ("binary_sensor", valid),
        ]:
            CORE.register_platform_component(platform, entity)
    for index, fixture in enumerate(config["fixtures"]):
        cg.add(
            var.set_record(
                index,
                config["_uids"][index],
                fixture[CONF_NAME],
                fixture["mac_address"].as_hex,
                fixture["lighting_id"],
                fixture["unit"],
                fixture["root_device"],
            )
        )


def final_validate(config):
    full = fv.full_config.get()
    seed = next(
        item
        for item in full["odelic"]
        if str(item[CONF_ID]) == str(config["odelic_id"])
    )
    client = next(
        item
        for item in full["ble_client"]
        if str(item[CONF_ID]) == str(seed["ble_client_id"])
    )
    if client["auto_connect"]:
        raise cv.Invalid(
            "odelic_controller requires seed ble_client auto_connect: false"
        )
    if (
        esp32.get_esp32_variant() == "ESP32S3"
        and full["esp32_ble"]["max_connections"] > MAX_LIGHTS
    ):
        raise cv.Invalid(
            "ESP32-S3 Odelic builds support at most eight BLE connection slots"
        )
    used = len(CORE.data[esp32_ble.KEY_ESP32_BLE][esp32_ble.KEY_USED_CONNECTION_SLOTS])
    if full["esp32_ble"]["max_connections"] < used:
        raise cv.Invalid(
            f"roster and other BLE clients require {used} simultaneous connections; "
            "increase esp32_ble.max_connections or reduce max_lights"
        )
    return config


FINAL_VALIDATE_SCHEMA = final_validate
