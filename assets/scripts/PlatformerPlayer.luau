--!strict

type Fields = {
    movement_axis: string,
    jump_action: string,
    walk_speed: number,
    jump_speed: number,
}

type ScriptContext = {
    entity: number,
    fields: Fields,
    get_axis: (string) -> number,
    is_action_just_pressed: (string) -> boolean,
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
    on_update: ((ScriptContext, number) -> ())?,
}

local script: ScriptDefinition = {
    fields = {
        { name = "movement_axis", type = "string", default = "Horizontal" },
        { name = "jump_action", type = "string", default = "Jump" },
        { name = "walk_speed", type = "number", default = 280.0 },
        { name = "jump_speed", type = "number", default = 520.0 },
    },
}

function script.on_update(context: ScriptContext, _delta_time: number)
    local movement = context.get_axis(context.fields.movement_axis)
    local _, velocity_y = context.get_velocity()
    local grounded = context.is_grounded()

    if movement < 0.0 then
        context.set_sprite_flip_x(true)
    elseif movement > 0.0 then
        context.set_sprite_flip_x(false)
    end

    if grounded and context.is_action_just_pressed(context.fields.jump_action) then
        velocity_y = context.fields.jump_speed
        grounded = false
    end

    local velocity_x = movement * context.fields.walk_speed
    context.set_velocity(velocity_x, velocity_y)

    if not grounded then
        if velocity_y > 20.0 then
            context.play_animation("Jump")
        elseif velocity_y < -20.0 then
            context.play_animation("Fall")
        end
    elseif math.abs(velocity_x) > 10.0 then
        context.play_animation("Run")
    else
        context.play_animation("Idle")
    end
end

return script
