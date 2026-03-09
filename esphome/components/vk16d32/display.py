import esphome.codegen as cg
from esphome.components import display, i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_INTENSITY, CONF_LAMBDA

CODEOWNERS = ["@tuct"]
DEPENDENCIES = ["i2c"]

vk16d32_ns = cg.esphome_ns.namespace("vk16d32")
VK16D32Display = vk16d32_ns.class_("VK16D32Display", cg.PollingComponent, i2c.I2CDevice)
VK16D32DisplayRef = VK16D32Display.operator("ref")

CONF_NUM_GRIDS = "num_grids"

CONFIG_SCHEMA = (
    display.BASIC_DISPLAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(VK16D32Display),
            # brightness: 0 (min ~4.4 mA) … 7 (max 35 mA, default)
            cv.Optional(CONF_INTENSITY, default=7): cv.All(
                cv.uint8_t, cv.Range(min=0, max=7)
            ),
            # num_grids: number of active GRID outputs to scan (1–12)
            cv.Optional(CONF_NUM_GRIDS, default=12): cv.All(
                cv.uint8_t, cv.Range(min=1, max=12)
            ),
        }
    )
    .extend(cv.polling_component_schema("1s"))
    # Default 7-bit I²C address 0x42 (ADDR pin = GND); use 0x43 for ADDR = VDD.
    .extend(i2c.i2c_device_schema(0x42))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await display.register_display(var, config)
    await i2c.register_i2c_device(var, config)

    cg.add(var.set_brightness(config[CONF_INTENSITY]))
    cg.add(var.set_num_grids(config[CONF_NUM_GRIDS]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(VK16D32DisplayRef, "it")], return_type=cg.void
        )
        cg.add(var.set_writer(lambda_))
