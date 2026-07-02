# SPDX-License-Identifier: GPL-3.0-only
# Derived from ESPHome pulse_meter component.

from esphome import automation, pins
import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_INTERNAL_FILTER,
    CONF_INTERNAL_FILTER_MODE,
    CONF_NUMBER,
    CONF_PIN,
    CONF_TIMEOUT,
    CONF_TOTAL,
    CONF_VALUE,
    ICON_PULSE,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_PULSES,
    UNIT_PULSES_PER_MINUTE,
)
from esphome.core import CORE

CODEOWNERS = ["@a"]

CONF_FLOW_LAMBDA = "flow_lambda"
CONF_VOLUME_LAMBDA = "volume_lambda"
CONF_MAX_TIME_DELTA = "max_time_delta"

optimized_pulse_meter_ns = cg.esphome_ns.namespace("optimized_pulse_meter")
OptimizedPulseMeterSensor = optimized_pulse_meter_ns.class_(
    "OptimizedPulseMeterSensor", sensor.Sensor, cg.Component
)
OptimizedPulseMeterInternalFilterMode = OptimizedPulseMeterSensor.enum("InternalFilterMode")

FILTER_MODES = {
    "EDGE": OptimizedPulseMeterInternalFilterMode.FILTER_EDGE,
    "PULSE": OptimizedPulseMeterInternalFilterMode.FILTER_PULSE,
}

SetTotalAction = optimized_pulse_meter_ns.class_("SetTotalAction", automation.Action)


def validate_internal_filter(value):
    return cv.positive_time_period_microseconds(value)


def validate_timeout(value):
    value = cv.positive_time_period_microseconds(value)
    if value.total_minutes > 70:
        raise cv.Invalid("Maximum timeout is 70 minutes")
    return value


def validate_max_time_delta(value):
    return cv.positive_time_period_microseconds(value)


def validate_optimized_pulse_meter_pin(value):
    value = pins.internal_gpio_input_pin_schema(value)
    if CORE.is_esp8266 and value[CONF_NUMBER] >= 16:
        raise cv.Invalid(
            "Pins GPIO16 and GPIO17 cannot be used as pulse counters on ESP8266."
        )
    return value


CONFIG_SCHEMA = sensor.sensor_schema(
    OptimizedPulseMeterSensor,
    unit_of_measurement=UNIT_PULSES_PER_MINUTE,
    icon=ICON_PULSE,
    accuracy_decimals=4,
    state_class=STATE_CLASS_MEASUREMENT,
).extend(
    {
        cv.Required(CONF_PIN): validate_optimized_pulse_meter_pin,
        cv.Optional(CONF_INTERNAL_FILTER, default="13us"): validate_internal_filter,
        cv.Optional(CONF_TIMEOUT, default="5min"): validate_timeout,
        cv.Optional(CONF_MAX_TIME_DELTA, default="500ms"): validate_max_time_delta,
        cv.Optional(CONF_TOTAL): sensor.sensor_schema(
            unit_of_measurement=UNIT_PULSES,
            icon=ICON_PULSE,
            accuracy_decimals=5,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        ),
        cv.Optional(CONF_INTERNAL_FILTER_MODE, default="EDGE"): cv.enum(
            FILTER_MODES, upper=True
        ),
        cv.Optional(CONF_FLOW_LAMBDA): cv.returning_lambda,
        cv.Optional(CONF_VOLUME_LAMBDA): cv.returning_lambda,
    }
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    await cg.register_component(var, config)

    pin = await cg.gpio_pin_expression(config[CONF_PIN])
    cg.add(var.set_pin(pin))
    cg.add(var.set_filter_us(config[CONF_INTERNAL_FILTER]))
    cg.add(var.set_timeout_us(config[CONF_TIMEOUT]))
    cg.add(var.set_max_time_delta_us(config[CONF_MAX_TIME_DELTA]))
    cg.add(var.set_filter_mode(config[CONF_INTERNAL_FILTER_MODE]))

    if CONF_FLOW_LAMBDA in config:
        template_ = await cg.process_lambda(
            config[CONF_FLOW_LAMBDA], [(cg.float_, "x")], return_type=cg.float_
        )
        cg.add(var.set_flow_lambda(template_))

    if CONF_VOLUME_LAMBDA in config:
        template_ = await cg.process_lambda(
            config[CONF_VOLUME_LAMBDA], [(cg.float_, "x")], return_type=cg.float_
        )
        cg.add(var.set_volume_lambda(template_))

    if CONF_TOTAL in config:
        sens = await sensor.new_sensor(config[CONF_TOTAL])
        cg.add(var.set_total_sensor(sens))


@automation.register_action(
    "optimized_pulse_meter.set_total",
    SetTotalAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(OptimizedPulseMeterSensor),
            cv.Required(CONF_VALUE): cv.templatable(cv.float_),
        }
    ),
    synchronous=True,
)
async def set_total_action_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, paren)
    template_ = await cg.templatable(config[CONF_VALUE], args, cg.float_)
    cg.add(var.set_total(template_))
    return var
