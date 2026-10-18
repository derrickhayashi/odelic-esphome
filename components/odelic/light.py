import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_OUTPUT_ID

from . import OdelicHub, ns

DEPENDENCIES = ["odelic"]
OdelicLight = ns.class_("OdelicLight", light.LightOutput)
CONFIG_SCHEMA = light.light_schema(OdelicLight, light.LightType.BRIGHTNESS_ONLY).extend(
    {
        cv.Required("odelic_id"): cv.use_id(OdelicHub),
        cv.Optional(
            "default_transition_length", default="0s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional("gamma_correct", default=1.0): cv.positive_float,
        cv.Optional("restore_mode", default="ALWAYS_OFF"): cv.enum(
            light.RESTORE_MODES, upper=True, space="_"
        ),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await light.register_light(var, config)
    hub = await cg.get_variable(config["odelic_id"])
    cg.add(var.set_parent(hub))
    cg.add(hub.set_light(var))
