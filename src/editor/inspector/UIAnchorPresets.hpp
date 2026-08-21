#pragma once

#include "../../ecs/components/UIComponents.hpp"
#include "../../ui/CanvasLayout.hpp"

#include <array>
#include <cmath>
#include <optional>

namespace Engine {

enum class UIAnchorPreset {
    TopLeft,
    TopCenter,
    TopRight,
    TopStretch,
    MiddleLeft,
    Center,
    MiddleRight,
    MiddleStretch,
    BottomLeft,
    BottomCenter,
    BottomRight,
    BottomStretch,
    LeftStretch,
    CenterStretch,
    RightStretch,
    FullStretch
};

struct UIAnchorPresetDefinition {
    UIAnchorPreset preset;
    const char* name;
    glm::vec2 anchor_min;
    glm::vec2 anchor_max;
};

struct UIAnchorPresetModifiers {
    bool set_position_and_size{false};
    bool set_pivot{false};
};

inline const std::array<UIAnchorPresetDefinition, 16>& ui_anchor_preset_definitions() {
    static const std::array<UIAnchorPresetDefinition, 16> definitions{{
        {UIAnchorPreset::TopLeft, "Top Left", {0.0f, 0.0f}, {0.0f, 0.0f}},
        {UIAnchorPreset::TopCenter, "Top Center", {0.5f, 0.0f}, {0.5f, 0.0f}},
        {UIAnchorPreset::TopRight, "Top Right", {1.0f, 0.0f}, {1.0f, 0.0f}},
        {UIAnchorPreset::TopStretch, "Top Stretch", {0.0f, 0.0f}, {1.0f, 0.0f}},
        {UIAnchorPreset::MiddleLeft, "Middle Left", {0.0f, 0.5f}, {0.0f, 0.5f}},
        {UIAnchorPreset::Center, "Center", {0.5f, 0.5f}, {0.5f, 0.5f}},
        {UIAnchorPreset::MiddleRight, "Middle Right", {1.0f, 0.5f}, {1.0f, 0.5f}},
        {UIAnchorPreset::MiddleStretch, "Middle Stretch", {0.0f, 0.5f}, {1.0f, 0.5f}},
        {UIAnchorPreset::BottomLeft, "Bottom Left", {0.0f, 1.0f}, {0.0f, 1.0f}},
        {UIAnchorPreset::BottomCenter, "Bottom Center", {0.5f, 1.0f}, {0.5f, 1.0f}},
        {UIAnchorPreset::BottomRight, "Bottom Right", {1.0f, 1.0f}, {1.0f, 1.0f}},
        {UIAnchorPreset::BottomStretch, "Bottom Stretch", {0.0f, 1.0f}, {1.0f, 1.0f}},
        {UIAnchorPreset::LeftStretch, "Left Stretch", {0.0f, 0.0f}, {0.0f, 1.0f}},
        {UIAnchorPreset::CenterStretch, "Center Stretch", {0.5f, 0.0f}, {0.5f, 1.0f}},
        {UIAnchorPreset::RightStretch, "Right Stretch", {1.0f, 0.0f}, {1.0f, 1.0f}},
        {UIAnchorPreset::FullStretch, "Full Stretch", {0.0f, 0.0f}, {1.0f, 1.0f}}
    }};
    return definitions;
}

inline std::optional<UIAnchorPreset> classify_ui_anchor_preset(
    const UILayoutComponent& layout,
    float epsilon = 0.001f
) {
    for (const auto& definition : ui_anchor_preset_definitions()) {
        if (glm::all(glm::lessThanEqual(
                glm::abs(layout.anchor_min - definition.anchor_min), glm::vec2(epsilon)
            )) &&
            glm::all(glm::lessThanEqual(
                glm::abs(layout.anchor_max - definition.anchor_max), glm::vec2(epsilon)
            ))) {
            return definition.preset;
        }
    }
    return std::nullopt;
}

inline UILayoutComponent apply_ui_anchor_preset(
    const UILayoutComponent& layout,
    const UIRect& parent_rect,
    const UIAnchorPresetDefinition& preset,
    const UIAnchorPresetModifiers& modifiers = {}
) {
    const UIRect current_rect = resolve_ui_rect(layout, parent_rect);
    UILayoutComponent result = layout;
    result.anchor_min = preset.anchor_min;
    result.anchor_max = preset.anchor_max;

    if (modifiers.set_pivot) {
        result.pivot = (preset.anchor_min + preset.anchor_max) * 0.5f;
    }

    if (modifiers.set_position_and_size) {
        result.anchored_position = {0.0f, 0.0f};
        result.size_delta.x = preset.anchor_min.x == preset.anchor_max.x
            ? current_rect.size.x
            : 0.0f;
        result.size_delta.y = preset.anchor_min.y == preset.anchor_max.y
            ? current_rect.size.y
            : 0.0f;
    }
    return result;
}

} // namespace Engine
