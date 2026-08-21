#pragma once

#include "../EcsTypes.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../renderer/RenderView.hpp"
#include "../../renderer/RenderLayer.hpp"
#include "../../renderer/Texture.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

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
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};

    SpriteComponent() = default;
    explicit SpriteComponent(
        Texture* texture_value,
        const glm::vec2& size_value = {64.0f, 64.0f},
        SDL_Color color_value = {255, 255, 255, 255}
    ) : texture(texture_value), color(color_value), size(size_value) {}
};

// Scene-owned assignment of a reusable .layereffect recipe. Runtime passes are
// resolved outside the component so snapshots and serialized data stay pure.
struct RenderEffectComponent {
    AssetHandle profile_handle{};
    bool enabled{true};

    bool operator==(const RenderEffectComponent&) const = default;
};

enum class Light2DType {
    Ambient,
    Point
};

enum class Light2DBlendMode {
    Additive,
    Multiply,
    Screen
};

// Pure authored lighting data. Position comes from TransformComponent and the
// runtime pass is built by Lighting2DSystem, never serialized into the scene.
struct Light2DComponent {
    Light2DType type{Light2DType::Point};
    SDL_Color color{255, 244, 214, 255};
    float intensity{1.0f};
    float radius{256.0f};
    float falloff{1.0f};
    Light2DBlendMode blend_mode{Light2DBlendMode::Additive};
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    bool casts_shadows{true};
    bool enabled{true};

    bool operator==(const Light2DComponent& other) const {
        return type == other.type &&
            color.r == other.color.r && color.g == other.color.g &&
            color.b == other.color.b && color.a == other.color.a &&
            intensity == other.intensity && radius == other.radius && falloff == other.falloff &&
            blend_mode == other.blend_mode && affected_layers == other.affected_layers &&
            casts_shadows == other.casts_shadows && enabled == other.enabled;
    }
};

enum class ShadowGeometrySource {
    Collider,
    StaticPolygon
};

// Shadow casting is presentation data kept separate from physics. Collider
// mode reuses authored collider geometry; StaticPolygon owns local vertices.
struct ShadowCaster2DComponent {
    ShadowGeometrySource source{ShadowGeometrySource::Collider};
    std::vector<glm::vec2> vertices{
        {-32.0f, -32.0f},
        {32.0f, -32.0f},
        {32.0f, 32.0f},
        {-32.0f, 32.0f}
    };
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    bool enabled{true};

    bool operator==(const ShadowCaster2DComponent&) const = default;
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
    RenderLayerMask render_layer_mask{ALL_RENDER_LAYERS};

    CameraComponent() = default;
    bool operator==(const CameraComponent& other) const {
        return is_primary == other.is_primary &&
            clear_color.r == other.clear_color.r && clear_color.g == other.clear_color.g &&
            clear_color.b == other.clear_color.b && clear_color.a == other.clear_color.a &&
            zoom == other.zoom && projection == other.projection &&
            follow_target == other.follow_target && follow_offset == other.follow_offset &&
            follow_speed == other.follow_speed && enable_bounds == other.enable_bounds &&
            bounds == other.bounds && render_layer_mask == other.render_layer_mask;
    }
};

} // namespace Engine
