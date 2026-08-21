#pragma once

#include "CanvasLayout.hpp"

namespace Engine {

class AssetDatabase;

[[nodiscard]] float resolve_ui_length(const UILength& length, float reference_size);

// Returns left, top, right, bottom in the same coordinate space as rect_size.
[[nodiscard]] glm::vec4 resolve_ui_edge_lengths(
    const UIEdgeLengths& edges,
    const glm::vec2& rect_size
);

[[nodiscard]] UIRect calculate_ui_text_content_rect(
    const UIRect& screen_rect,
    const UITextComponent& text,
    const glm::vec2& canvas_scale
);

// Calculates the logical Rect Transform size after optional content fitting.
// Percentage margins are solved against the resulting fitted size.
[[nodiscard]] glm::vec2 calculate_ui_text_fitted_size(
    const UITextComponent& text,
    const glm::vec2& authored_size,
    const AssetDatabase* asset_database = nullptr
);

} // namespace Engine
