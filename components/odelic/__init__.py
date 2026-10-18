"""Odelic Connected Lighting BLE protocol hub."""

import re

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, ble_client
from esphome.const import CONF_ID

DOMAIN = "odelic"
DEPENDENCIES = ["ble_client"]
AUTO_LOAD = ["binary_sensor"]
MULTI_CONF = True
ns = cg.esphome_ns.namespace(DOMAIN)
OdelicHub = ns.class_("OdelicHub", cg.PollingComponent, ble_client.BLEClientNode)


def lighting_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9]{8}", value):
        raise cv.Invalid("lighting_id must be a quoted eight-digit string")
    return value


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OdelicHub),
            cv.Required("lighting_id"): lighting_id,
            cv.Optional("unit", default=0): cv.int_range(min=0, max=255),
            cv.Optional("state_valid"): binary_sensor.binary_sensor_schema(
                entity_category="diagnostic"
            ),
        }
    )
    .extend(ble_client.BLE_CLIENT_SCHEMA)
    .extend(cv.polling_component_schema("5s"))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)
    cg.add(var.set_lighting_id(config["lighting_id"]))
    cg.add(var.set_unit(config["unit"]))
    if "state_valid" in config:
        sens = await binary_sensor.new_binary_sensor(config["state_valid"])
        cg.add(var.set_state_valid(sens))
