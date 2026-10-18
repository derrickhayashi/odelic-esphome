"""Shared static/runtime Odelic fixture roster and scheduler."""

import zlib

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, light, select
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
        cv.GenerateID("state_id"): cv.declare_id(RosterLightState),
        cv.GenerateID("output_id"): cv.declare_id(OdelicOutput),
        cv.GenerateID("preset_id"): cv.declare_id(RosterPreset),
        cv.GenerateID("valid_id"): cv.declare_id(RosterValid),
    }
)


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
    return config


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
    cg.add(var.set_hub(hub))
    cg.add(var.set_capacity(len(config["_slots"])))
    cg.add_define("USE_DEVICES")
    cg.add_define("ESPHOME_DEVICE_COUNT", len(config["_slots"]))
    for index, slot in enumerate(config["_slots"]):
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
