import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import CONF_ID

from . import OdelicHub, ns

DEPENDENCIES = ["odelic"]
OdelicPreset = ns.class_("OdelicPreset", select.Select)
OPTIONS = ["Warm white", "Natural white", "Cool white", "Warm/Cool cool endpoint"]
CONFIG_SCHEMA = select.select_schema(OdelicPreset).extend(
    {
        cv.Required("odelic_id"): cv.use_id(OdelicHub),
    }
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await select.register_select(var, config, options=OPTIONS)
    hub = await cg.get_variable(config["odelic_id"])
    cg.add(var.set_parent(hub))
    cg.add(hub.set_preset(var))
