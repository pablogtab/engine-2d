#pragma once

#include "../core/Display.hpp"
#include <algorithm>
#include <glm/glm.hpp>

namespace Engine {

enum class RenderViewKind {
    Editor,
    Game
};

enum class PresentationMode {
    Fit,
    Fill,
    Stretch,
    Responsive
};

enum class CameraProjectionMode {
    FixedHeight,
    FixedWidth,
    Expand,
    PixelPerfect
};

struct RenderViewConfiguration {
    int output_width{1280};
    int output_height{720};
    int reference_width{1280};
    int reference_height{720};
    SafeAreaInsets safe_area{};
    bool show_safe_area_overlay{false};

    bool operator==(const RenderViewConfiguration&) const = default;
};

struct PresentationMapping {
    glm::vec2 display_offset{0.0f};
    glm::vec2 display_size{1.0f};
    glm::vec2 uv_min{0.0f};
    glm::vec2 uv_max{1.0f};
};

[[nodiscard]] inline PresentationMapping calculate_presentation(
    const glm::vec2& source_size,
    const glm::vec2& destination_size,
    PresentationMode mode
) {
    PresentationMapping mapping;
    mapping.display_size = glm::max(destination_size, glm::vec2(1.0f));
    const float source_aspect = source_size.x / std::max(source_size.y, 1.0f);
    const float destination_aspect = destination_size.x / std::max(destination_size.y, 1.0f);

    if (mode == PresentationMode::Fit) {
        if (destination_aspect > source_aspect) {
            mapping.display_size.x = destination_size.y * source_aspect;
        } else {
            mapping.display_size.y = destination_size.x / source_aspect;
        }
        mapping.display_offset = (destination_size - mapping.display_size) * 0.5f;
    } else if (mode == PresentationMode::Fill) {
        if (destination_aspect > source_aspect) {
            const float visible_height = source_aspect / destination_aspect;
            mapping.uv_min.y = (1.0f - visible_height) * 0.5f;
            mapping.uv_max.y = 1.0f - mapping.uv_min.y;
        } else {
            const float visible_width = destination_aspect / source_aspect;
            mapping.uv_min.x = (1.0f - visible_width) * 0.5f;
            mapping.uv_max.x = 1.0f - mapping.uv_min.x;
        }
    }

    return mapping;
}

} // namespace Engine
