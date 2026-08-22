#pragma once

#include <string_view>

namespace Engine {

inline constexpr std::string_view DEFAULT_SCRIPT_TEMPLATE = R"(--!strict

type Fields = {
    speed: number,
}

type ScriptContext = {
    entity: number,
    fields: Fields,
    get_position: () -> (number, number, number),
    set_position: (number, number, number?) -> (),
    get_axis: (string) -> number,
    is_action_pressed: (string) -> boolean,
    is_action_just_pressed: (string) -> boolean,
    is_action_just_released: (string) -> boolean,
    get_velocity: () -> (number, number),
    set_velocity: (number, number) -> (),
    is_grounded: () -> boolean,
    set_sprite_flip_x: (boolean) -> (),
    play_animation: (string) -> boolean,
}

type FieldDefinition = {
    name: string,
    type: string,
    default: any,
}

type ScriptDefinition = {
    fields: {FieldDefinition},
    on_create: ((ScriptContext) -> ())?,
    on_update: ((ScriptContext, number) -> ())?,
    on_destroy: ((ScriptContext) -> ())?,
}

local script: ScriptDefinition = {
    fields = {
        { name = "speed", type = "number", default = 100.0 },
    },
}

function script.on_create(context: ScriptContext)
    print(`Created entity {context.entity}`)
end

function script.on_update(context: ScriptContext, delta_time: number)
    local x, y, z = context.get_position()
    context.set_position(x + context.fields.speed * delta_time, y, z)
end

function script.on_destroy(context: ScriptContext)
    print(`Destroyed entity {context.entity}`)
end

return script
)";

} // namespace Engine
