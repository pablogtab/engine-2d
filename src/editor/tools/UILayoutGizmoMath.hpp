#pragma once

#include "../../ui/CanvasLayout.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace Engine {

enum class UIResizeHandle {
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft,
    Left
};

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

inline float snap_ui_value(float value, bool enabled, float step) {
    return enabled && step > 0.0f
        ? std::round(value / step) * step
        : value;
}

inline UILayoutComponent layout_for_ui_rect(
    UILayoutComponent layout,
    const UIRect& parent_rect,
    const UIRect& rect
) {
    const glm::vec2 anchor_min = parent_rect.position + parent_rect.size * layout.anchor_min;
    const glm::vec2 anchor_max = parent_rect.position + parent_rect.size * layout.anchor_max;
    const glm::vec2 anchor_pivot = glm::mix(anchor_min, anchor_max, layout.pivot);
    layout.size_delta = rect.size - (anchor_max - anchor_min);
    layout.anchored_position = rect.position + rect.size * layout.pivot - anchor_pivot;
    return layout;
}

inline UIRect resize_ui_rect(
    const UIRect& initial,
    const glm::vec2& delta,
    UIResizeHandle handle,
    bool snap_enabled = false,
    float snap_step = 0.0f,
    float minimum_size = 1.0f
) {
    glm::vec2 minimum = initial.min();
    glm::vec2 maximum = initial.max();
    const bool moves_left = handle == UIResizeHandle::TopLeft ||
        handle == UIResizeHandle::Left || handle == UIResizeHandle::BottomLeft;
    const bool moves_right = handle == UIResizeHandle::TopRight ||
        handle == UIResizeHandle::Right || handle == UIResizeHandle::BottomRight;
    const bool moves_top = handle == UIResizeHandle::TopLeft ||
        handle == UIResizeHandle::Top || handle == UIResizeHandle::TopRight;
    const bool moves_bottom = handle == UIResizeHandle::BottomLeft ||
        handle == UIResizeHandle::Bottom || handle == UIResizeHandle::BottomRight;

    if (moves_left) {
        minimum.x = std::min(
            snap_ui_value(initial.position.x + delta.x, snap_enabled, snap_step),
            maximum.x - minimum_size
        );
    }
    if (moves_right) {
        maximum.x = std::max(
            snap_ui_value(initial.max().x + delta.x, snap_enabled, snap_step),
            minimum.x + minimum_size
        );
    }
    if (moves_top) {
        minimum.y = std::min(
            snap_ui_value(initial.position.y + delta.y, snap_enabled, snap_step),
            maximum.y - minimum_size
        );
    }
    if (moves_bottom) {
        maximum.y = std::max(
            snap_ui_value(initial.max().y + delta.y, snap_enabled, snap_step),
            minimum.y + minimum_size
        );
    }
    return {minimum, maximum - minimum};
}

inline UILayoutComponent move_ui_pivot_preserving_rect(
    const UILayoutComponent& initial,
    const UIRect& parent_rect,
    const UIRect& rect,
    const glm::vec2& pivot
) {
    UILayoutComponent result = initial;
    result.pivot = glm::clamp(pivot, glm::vec2(0.0f), glm::vec2(1.0f));
    return layout_for_ui_rect(result, parent_rect, rect);
}

inline UILayoutComponent move_ui_anchors_preserving_rect(
    const UILayoutComponent& initial,
    const UIRect& parent_rect,
    const UIRect& rect,
    const glm::vec2& anchor_min,
    const glm::vec2& anchor_max
) {
    UILayoutComponent result = initial;
    result.anchor_min = glm::clamp(anchor_min, glm::vec2(0.0f), glm::vec2(1.0f));
    result.anchor_max = glm::clamp(anchor_max, result.anchor_min, glm::vec2(1.0f));
    return layout_for_ui_rect(result, parent_rect, rect);
}

} // namespace Engine
