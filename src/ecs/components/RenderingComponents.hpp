#pragma once

#include "../EcsTypes.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../renderer/RenderView.hpp"
#include "../../renderer/Texture.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace Engine {

class Material;

struct SpriteComponent {
    Texture* texture{nullptr};
    AssetHandle texture_handle{};
    SDL_Color color{255, 255, 255, 255};
    SDL_FRect src_rect{0.0f, 0.0f, 0.0f, 0.0f};
    bool use_src_rect{false};
    bool visible{true};
    glm::vec2 size{64.0f, 64.0f};
    std::shared_ptr<const Material> material;
    AssetHandle material_handle{};

    bool has_outline{false};
    SDL_Color outline_color{255, 215, 0, 255};
    float outline_thickness{2.0f};
    bool outline_pulse{false};

    bool flip_x{false};
    bool flip_y{false};
    std::int16_t sorting_layer{0};
    std::int16_t order_in_layer{0};

    SpriteComponent() = default;
    explicit SpriteComponent(
        Texture* texture_value,
        const glm::vec2& size_value = {64.0f, 64.0f},
        SDL_Color color_value = {255, 255, 255, 255}
    ) : texture(texture_value), color(color_value), size(size_value) {}
};

struct CameraComponent {
    bool is_primary{false};
    SDL_Color clear_color{24, 28, 36, 255};
    float zoom{1.0f};
    CameraProjectionMode projection{CameraProjectionMode::FixedHeight};
    EntityID follow_target{0};
    glm::vec2 follow_offset{0.0f, 0.0f};
    float follow_speed{6.0f};
    bool enable_bounds{false};
    glm::vec4 bounds{-2000.0f, -1000.0f, 2000.0f, 1000.0f};

    CameraComponent() = default;
    bool operator==(const CameraComponent& other) const {
        return is_primary == other.is_primary &&
            clear_color.r == other.clear_color.r && clear_color.g == other.clear_color.g &&
            clear_color.b == other.clear_color.b && clear_color.a == other.clear_color.a &&
            zoom == other.zoom && projection == other.projection &&
            follow_target == other.follow_target && follow_offset == other.follow_offset &&
            follow_speed == other.follow_speed && enable_bounds == other.enable_bounds &&
            bounds == other.bounds;
    }
};

} // namespace Engine
