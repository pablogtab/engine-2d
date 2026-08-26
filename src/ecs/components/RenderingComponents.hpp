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
    Point,
    Spot,
    Directional,
    Area
};

enum class Light2DMobility {
    Realtime,
    Baked
};

enum class ShadowMode {
    None,
    Hard,
    SoftFast,
    ContactHardening
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
    float range{256.0f};
    float falloff{1.0f};
    float height{64.0f};
    float spot_inner_angle{25.0f};
    float spot_outer_angle{45.0f};
    glm::vec2 area_size{96.0f, 32.0f};
    float source_radius{0.0f};
    float directional_angular_radius{0.25f};
    Light2DBlendMode blend_mode{Light2DBlendMode::Additive};
    Light2DMobility mobility{Light2DMobility::Realtime};
    ShadowMode shadow_mode{ShadowMode::Hard};
    SDL_Color shadow_color{0, 0, 0, 255};
    float shadow_strength{1.0f};
    float shadow_softness{0.5f};
    float shadow_bias{0.5f};
    float shadow_max_distance{1024.0f};
    std::int16_t shadow_priority{0};
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    bool enabled{true};

    bool operator==(const Light2DComponent& other) const {
        return type == other.type &&
            color.r == other.color.r && color.g == other.color.g &&
            color.b == other.color.b && color.a == other.color.a &&
            intensity == other.intensity && range == other.range && falloff == other.falloff &&
            height == other.height && spot_inner_angle == other.spot_inner_angle &&
            spot_outer_angle == other.spot_outer_angle && area_size == other.area_size &&
            source_radius == other.source_radius &&
            directional_angular_radius == other.directional_angular_radius &&
            blend_mode == other.blend_mode && mobility == other.mobility &&
            shadow_mode == other.shadow_mode &&
            shadow_color.r == other.shadow_color.r && shadow_color.g == other.shadow_color.g &&
            shadow_color.b == other.shadow_color.b && shadow_color.a == other.shadow_color.a &&
            shadow_strength == other.shadow_strength && shadow_softness == other.shadow_softness &&
            shadow_bias == other.shadow_bias && shadow_max_distance == other.shadow_max_distance &&
            shadow_priority == other.shadow_priority && affected_layers == other.affected_layers &&
            enabled == other.enabled;
    }
};

enum class ShadowGeometrySource {
    Collider,
    StaticPolygon,
    SpriteAlphaContour
};

enum class ShadowCasterMobility {
    Realtime,
    Baked
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
    ShadowCasterMobility mobility{ShadowCasterMobility::Realtime};
    float height{32.0f};
    float opacity{1.0f};
    float alpha_threshold{0.5f};
    float contour_tolerance{1.0f};
    bool enabled{true};

    bool operator==(const ShadowCaster2DComponent&) const = default;
};

struct SpriteLighting2DComponent {
    AssetHandle normal_map_handle{};
    AssetHandle emissive_map_handle{};
    AssetHandle specular_map_handle{};
    // Runtime-only resolved assets. Handles above remain the serialized contract.
    Texture* normal_map{nullptr};
    Texture* emissive_map{nullptr};
    Texture* specular_map{nullptr};
    float normal_strength{1.0f};
    SDL_Color emissive_tint{255, 255, 255, 255};
    float emissive_intensity{0.0f};
    float surface_height{0.0f};
    float specular_strength{0.0f};
    float shininess{16.0f};
    bool receives_lighting{true};
    bool receives_shadows{true};
    bool baked_receiver{false};
    bool enabled{true};

    bool operator==(const SpriteLighting2DComponent& other) const {
        return normal_map_handle == other.normal_map_handle && emissive_map_handle == other.emissive_map_handle &&
            specular_map_handle == other.specular_map_handle &&
            normal_strength == other.normal_strength &&
            emissive_tint.r == other.emissive_tint.r && emissive_tint.g == other.emissive_tint.g &&
            emissive_tint.b == other.emissive_tint.b && emissive_tint.a == other.emissive_tint.a &&
            emissive_intensity == other.emissive_intensity && surface_height == other.surface_height &&
            specular_strength == other.specular_strength && shininess == other.shininess &&
            receives_lighting == other.receives_lighting && receives_shadows == other.receives_shadows &&
            baked_receiver == other.baked_receiver && enabled == other.enabled;
    }
};

enum class SpriteShadowMode {
    FixedContact,
    DominantLight
};

enum class SpriteShadowShape {
    Ellipse,
    Texture
};

struct SpriteShadow2DComponent {
    SpriteShadowMode mode{SpriteShadowMode::DominantLight};
    SpriteShadowShape shape{SpriteShadowShape::Ellipse};
    AssetHandle texture_handle{};
    // Runtime-only resolved texture. The handle remains the serialized contract.
    Texture* texture{nullptr};
    glm::vec2 offset{0.0f, 20.0f};
    glm::vec2 size{48.0f, 18.0f};
    float opacity{0.42f};
    float softness{0.65f};
    float elevation{0.0f};
    float max_projection{160.0f};
    RenderLayerMask light_layers{ALL_RENDER_LAYERS};
    std::int16_t sorting_offset{-1};
    bool enabled{true};

    bool operator==(const SpriteShadow2DComponent& other) const {
        return mode == other.mode && shape == other.shape && texture_handle == other.texture_handle &&
            offset == other.offset && size == other.size && opacity == other.opacity &&
            softness == other.softness && elevation == other.elevation &&
            max_projection == other.max_projection && light_layers == other.light_layers &&
            sorting_offset == other.sorting_offset && enabled == other.enabled;
    }
};

struct LightingBakeVolume2DComponent {
    glm::vec2 size{1024.0f, 720.0f};
    float texels_per_unit{1.0f};
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    bool enabled{true};

    bool operator==(const LightingBakeVolume2DComponent&) const = default;
};

struct CameraLayerEffectAssignment {
    AssetHandle effect_handle{};
    RenderLayer render_layer{0};
    bool enabled{true};

    bool operator==(const CameraLayerEffectAssignment&) const = default;
};

struct CameraPostProcessSettings {
    bool enabled{true};
    AssetHandle global_profile_handle{};
    std::vector<CameraLayerEffectAssignment> layer_effects{};

    bool operator==(const CameraPostProcessSettings&) const = default;
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
    CameraPostProcessSettings post_process{};

    CameraComponent() = default;
    bool operator==(const CameraComponent& other) const {
        return is_primary == other.is_primary &&
            clear_color.r == other.clear_color.r && clear_color.g == other.clear_color.g &&
            clear_color.b == other.clear_color.b && clear_color.a == other.clear_color.a &&
            zoom == other.zoom && projection == other.projection &&
            follow_target == other.follow_target && follow_offset == other.follow_offset &&
            follow_speed == other.follow_speed && enable_bounds == other.enable_bounds &&
            bounds == other.bounds && render_layer_mask == other.render_layer_mask &&
            post_process == other.post_process;
    }
};

} // namespace Engine
