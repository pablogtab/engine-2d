#pragma once

#include "../../core/AssetHandle.hpp"
#include "../../renderer/RenderView.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <string>

namespace Engine {

class Material;

enum class UIHorizontalAlignment {
    Left,
    Center,
    Right
};

enum class UIVerticalAlignment {
    Top,
    Center,
    Bottom
};

enum class UIImageScaleMode {
    Stretch,
    Fit,
    Fill
};

enum class UILengthUnit {
    Pixels,
    Percent
};

struct UILength {
    float value{0.0f};
    UILengthUnit unit{UILengthUnit::Pixels};

    bool operator==(const UILength&) const = default;
};

struct UIEdgeLengths {
    UILength left;
    UILength top;
    UILength right;
    UILength bottom;

    bool operator==(const UIEdgeLengths&) const = default;
};

struct CanvasComponent {
    glm::vec2 reference_size{1280.0f, 720.0f};
    PresentationMode scaling_policy{PresentationMode::Responsive};
    std::int16_t sorting_order{0};
    bool use_safe_area{true};
    bool enabled{true};

    bool operator==(const CanvasComponent&) const = default;
};

// Screen-space rectangle authored relative to the nearest parent UI rectangle.
// Equal anchors create a fixed-size element; separated anchors stretch it.
struct UILayoutComponent {
    glm::vec2 anchor_min{0.5f, 0.5f};
    glm::vec2 anchor_max{0.5f, 0.5f};
    glm::vec2 pivot{0.5f, 0.5f};
    glm::vec2 anchored_position{0.0f, 0.0f};
    glm::vec2 size_delta{160.0f, 48.0f};

    bool operator==(const UILayoutComponent&) const = default;
};

struct UITextComponent {
    std::string text{"Text"};
    AssetHandle font_handle{};
    AssetHandle material_handle{};
    std::shared_ptr<const Material> material;
    float font_size{24.0f};
    SDL_Color color{255, 255, 255, 255};
    UIHorizontalAlignment horizontal_alignment{UIHorizontalAlignment::Center};
    UIVerticalAlignment vertical_alignment{UIVerticalAlignment::Center};
    // Text-area margins. Percentages resolve against this element's rect.
    UIEdgeLengths margins;
    bool fit_content_width{false};
    bool fit_content_height{false};
    // Left, top, right, and bottom in logical Canvas pixels.
    glm::vec4 fit_padding{0.0f};
    bool wrap{false};
    bool enabled{true};

    bool operator==(const UITextComponent& other) const {
        return text == other.text && font_handle == other.font_handle &&
            material_handle == other.material_handle && material == other.material &&
            font_size == other.font_size &&
            color.r == other.color.r && color.g == other.color.g &&
            color.b == other.color.b && color.a == other.color.a &&
            horizontal_alignment == other.horizontal_alignment &&
            vertical_alignment == other.vertical_alignment &&
            margins == other.margins &&
            fit_content_width == other.fit_content_width &&
            fit_content_height == other.fit_content_height &&
            fit_padding == other.fit_padding &&
            wrap == other.wrap && enabled == other.enabled;
    }
};

struct UIImageComponent {
    AssetHandle texture_handle{};
    AssetHandle material_handle{};
    std::shared_ptr<const Material> material;
    SDL_Color color{255, 255, 255, 255};
    UIImageScaleMode scale_mode{UIImageScaleMode::Stretch};
    bool raycast_target{true};
    bool enabled{true};

    bool operator==(const UIImageComponent& other) const {
        return texture_handle == other.texture_handle &&
            material_handle == other.material_handle && material == other.material &&
            color.r == other.color.r && color.g == other.color.g &&
            color.b == other.color.b && color.a == other.color.a &&
            scale_mode == other.scale_mode && raycast_target == other.raycast_target &&
            enabled == other.enabled;
    }
};

struct UIButtonComponent {
    SDL_Color normal_color{70, 76, 96, 255};
    SDL_Color hovered_color{90, 98, 124, 255};
    SDL_Color pressed_color{54, 60, 80, 255};
    SDL_Color disabled_color{70, 76, 96, 128};
    bool interactable{true};
    bool enabled{true};

    bool operator==(const UIButtonComponent& other) const {
        const auto same_color = [](const SDL_Color& lhs, const SDL_Color& rhs) {
            return lhs.r == rhs.r && lhs.g == rhs.g &&
                lhs.b == rhs.b && lhs.a == rhs.a;
        };
        return same_color(normal_color, other.normal_color) &&
            same_color(hovered_color, other.hovered_color) &&
            same_color(pressed_color, other.pressed_color) &&
            same_color(disabled_color, other.disabled_color) &&
            interactable == other.interactable && enabled == other.enabled;
    }
};

} // namespace Engine
