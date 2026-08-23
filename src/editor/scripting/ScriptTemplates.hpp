#pragma once

#include <string_view>

namespace Engine {

inline constexpr std::string_view DEFAULT_ENGINESCRIPT_TEMPLATE = R"(@behaviour
export class NewBehaviour extends Behaviour {
    @expose({ min: 0.0, max: 1000.0 })
    speed: Float = 200.0;

    override update(deltaTime: Float): Void {
    }
}
)";

inline constexpr std::string_view DEFAULT_SCRIPT_TEMPLATE = R"(--!strict

type Fields = {
    speed: number,
}

type DisplayInfo = {
    width: number,
    height: number,
    dpi_scale: number,
    reference_width: number,
    reference_height: number,
    window_mode: string,
    vsync: boolean,
    safe_area: {
        left: number,
        top: number,
        right: number,
        bottom: number,
    },
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
    play_sound: (string, number?, number?) -> (boolean, string?),
    play_music: (string, number?, boolean?) -> (boolean, string?),
    stop_music: () -> (),
    pause_music: () -> (),
    resume_music: () -> (),
    set_volume: ("master" | "music" | "sfx", number) -> (),
    get_volume: ("master" | "music" | "sfx") -> number,
    find_asset: (string) -> string?,
    asset_exists: (string) -> boolean,
    asset_path: (string) -> string?,
    asset_type: (string) -> string?,
    get_display_info: () -> DisplayInfo,
    set_window_mode: ("windowed" | "borderless_fullscreen" | "exclusive_fullscreen") -> (boolean, string?),
    set_vsync: (boolean) -> (boolean, string?),
    load_scene: (string) -> (boolean, string?),
    get_scene_name: () -> string,
    emit_event: (string, string?) -> (),
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
    on_collision_enter: ((ScriptContext, number, number, number, boolean) -> ())?,
    on_collision_exit: ((ScriptContext, number, boolean) -> ())?,
    on_ui_button_clicked: ((ScriptContext, number) -> ())?,
    on_ui_focus_changed: ((ScriptContext, number, number) -> ())?,
    on_event: ((ScriptContext, string, string, number) -> ())?,
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
