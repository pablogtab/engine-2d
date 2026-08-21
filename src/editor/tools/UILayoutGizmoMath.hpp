#pragma once

#include "../../ui/CanvasLayout.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

inline glm::vec2 calculate_ui_drag_position(
    const glm::vec2& initial_position,
    const glm::vec2& screen_delta,
    const CanvasViewport& viewport,
    bool snap_enabled = false,
    float snap_step = 0.0f
) {
    const glm::vec2 safe_scale = glm::max(glm::abs(viewport.scale), glm::vec2(0.0001f));
    glm::vec2 result = initial_position + screen_delta / safe_scale;
    if (snap_enabled && snap_step > 0.0f) {
        result.x = std::round(result.x / snap_step) * snap_step;
        result.y = std::round(result.y / snap_step) * snap_step;
    }
    return result;
}

} // namespace Engine
