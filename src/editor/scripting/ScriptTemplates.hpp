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

inline constexpr std::string_view DEFAULT_SCRIPT_TEMPLATE = DEFAULT_ENGINESCRIPT_TEMPLATE;

} // namespace Engine

