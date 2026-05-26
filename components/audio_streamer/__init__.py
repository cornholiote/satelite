from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import microphone
from esphome.const import CONF_ID, CONF_TRIGGER_ID

DEPENDENCIES = ["microphone", "network"]

CONF_HOST = "host"
CONF_PORT = "port"
CONF_MICROPHONE = "microphone"
CONF_SILENCE_THRESHOLD = "silence_threshold"
CONF_SILENCE_DURATION = "silence_duration"
CONF_MIN_DURATION = "min_duration"
CONF_MAX_DURATION = "max_duration"
CONF_ON_START = "on_start"
CONF_ON_END = "on_end"
CONF_ON_ERROR = "on_error"

audio_streamer_ns = cg.esphome_ns.namespace("audio_streamer")
AudioStreamer = audio_streamer_ns.class_("AudioStreamer", cg.Component)

StartAction = audio_streamer_ns.class_("StartAction", automation.Action)
StopAction = audio_streamer_ns.class_("StopAction", automation.Action)

StartTrigger = audio_streamer_ns.class_("StartTrigger", automation.Trigger.template())
EndTrigger = audio_streamer_ns.class_("EndTrigger", automation.Trigger.template())
ErrorTrigger = audio_streamer_ns.class_("ErrorTrigger", automation.Trigger.template())

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(AudioStreamer),
        cv.Required(CONF_MICROPHONE): cv.use_id(microphone.Microphone),
        cv.Required(CONF_HOST): cv.string,
        cv.Required(CONF_PORT): cv.port,
        cv.Optional(CONF_SILENCE_THRESHOLD, default=300): cv.int_range(min=0, max=32767),
        cv.Optional(
            CONF_SILENCE_DURATION, default="1500ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_MIN_DURATION, default="500ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_MAX_DURATION, default="0s"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_ON_START): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(StartTrigger)}
        ),
        cv.Optional(CONF_ON_END): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(EndTrigger)}
        ),
        cv.Optional(CONF_ON_ERROR): automation.validate_automation(
            {cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(ErrorTrigger)}
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    mic = await cg.get_variable(config[CONF_MICROPHONE])
    cg.add(var.set_microphone(mic))
    cg.add(var.set_host(config[CONF_HOST]))
    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_silence_threshold(config[CONF_SILENCE_THRESHOLD]))
    cg.add(var.set_silence_duration(config[CONF_SILENCE_DURATION].total_milliseconds))
    cg.add(var.set_min_duration(config[CONF_MIN_DURATION].total_milliseconds))
    cg.add(var.set_max_duration(config[CONF_MAX_DURATION].total_milliseconds))

    for conf in config.get(CONF_ON_START, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get(CONF_ON_END, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
    for conf in config.get(CONF_ON_ERROR, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)


@automation.register_action(
    "audio_streamer.start",
    StartAction,
    automation.maybe_simple_id({cv.Required(CONF_ID): cv.use_id(AudioStreamer)}),
)
async def audio_streamer_start_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)


@automation.register_action(
    "audio_streamer.stop",
    StopAction,
    automation.maybe_simple_id({cv.Required(CONF_ID): cv.use_id(AudioStreamer)}),
)
async def audio_streamer_stop_to_code(config, action_id, template_arg, args):
    paren = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, paren)
