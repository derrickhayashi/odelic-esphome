"""Optional, authenticated fixture setup and guided passive discovery."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import esp32_ble_tracker
from esphome.components.esp32_ble_tracker import CONF_ESP32_BLE_ID
from esphome.components.odelic_controller import OdelicController
from esphome.const import CONF_ID

# Local setup-page password policy; unrelated to the fixture's eight-digit lighting ID.
MIN_SETUP_PASSWORD_CHARS = 8

DEPENDENCIES = ["odelic_controller", "esp32_ble_tracker", "esp32", "wifi"]
AUTO_LOAD = ["web_server_base"]
ns = cg.esphome_ns.namespace("odelic_setup")
OdelicSetup = ns.class_(
    "OdelicSetup", cg.Component, esp32_ble_tracker.ESPBTDeviceListener
)


def password(value):
    value = cv.string_strict(value)
    if len(value) < MIN_SETUP_PASSWORD_CHARS:
        raise cv.Invalid("setup password must have at least eight characters")
    return value


# Keep these in sync with MAX_LIGHTS and the persisted layout in model.h.
MAX_LIGHTS = 8
# Stable NVS key: ASCII 'ODL1'. Changing it makes existing saved settings invisible.
DEFAULT_PREFERENCE_KEY = 0x4F444C31
CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(OdelicSetup),
            cv.Required("controller_id"): cv.use_id(OdelicController),
            cv.Required("username"): cv.All(cv.string_strict, cv.Length(min=1)),
            cv.Required("password"): password,
            cv.Optional("max_lights", default=MAX_LIGHTS): cv.int_range(
                min=1, max=MAX_LIGHTS
            ),
            cv.Optional("preference_key", default=DEFAULT_PREFERENCE_KEY): cv.uint32_t,
        }
    )
    .extend(esp32_ble_tracker.ESP_BLE_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await esp32_ble_tracker.register_ble_device(var, config)
    controller = await cg.get_variable(config["controller_id"])
    tracker = await cg.get_variable(config[CONF_ESP32_BLE_ID])
    cg.add(var.set_controller(controller))
    cg.add(var.set_tracker(tracker))
    cg.add(var.set_credentials(config["username"], config["password"]))
    cg.add(var.set_preference_key(config["preference_key"]))
    cg.add_define("USE_WEBSERVER_AUTH")
    cg.add(var.set_capacity(config["max_lights"]))


def final_validate(config):
    full = fv.full_config.get()
    if (
        len(full.get("odelic", [])) != 1
        or len(full.get("ble_client", [])) != 1
        or not full.get("odelic_controller")
    ):
        raise cv.Invalid(
            "runtime setup requires exactly one Odelic hub, controller and BLE client"
        )
    if full["ble_client"][0].get("auto_connect", True):
        raise cv.Invalid(
            "runtime setup requires ble_client auto_connect: false; saved settings enable it after boot"
        )
    controller = full["odelic_controller"]
    if controller.get("fixtures"):
        raise cv.Invalid(
            "runtime setup owns the fixture roster; do not configure static fixtures"
        )
    if controller["max_lights"] != config["max_lights"]:
        raise cv.Invalid("odelic_controller and odelic_setup max_lights must match")
    if full["esphome"].get("devices"):
        raise cv.Invalid(
            "runtime setup manages its own sub-devices; do not add esphome.devices"
        )
    if full.get("light") or full.get("select") or full.get("binary_sensor"):
        raise cv.Invalid(
            "runtime setup creates its own entities; use the runtime package without static light/select/sensor entries"
        )
    if "bluetooth_proxy" in full:
        raise cv.Invalid(
            "runtime discovery requires a dedicated BLE tracker, without bluetooth_proxy"
        )
    if full["esp32"]["framework"]["type"] != "esp-idf":
        raise cv.Invalid("runtime setup currently requires the ESP-IDF framework")
    return config


FINAL_VALIDATE_SCHEMA = final_validate
