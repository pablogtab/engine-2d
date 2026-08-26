#pragma once

#include "LightingPass.hpp"
#include "ShadowSilhouette.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include <algorithm>
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Engine {

class EntityManager;
class Renderer2D;

struct Light2DInstance {
    glm::vec2 world_position{0.0f};
    Light2DComponent light;
    EntityID entity{0};
    float world_rotation{0.0f};
};

struct ShadowPolygon2D {
    std::vector<glm::vec2> world_vertices;
    RenderLayerMask affected_layers{ALL_RENDER_LAYERS};
    float height{32.0f};
    float opacity{1.0f};
    EntityID entity{0};
};

[[nodiscard]] float point_light_falloff(
    float distance,
    float radius,
    float intensity,
    float exponent = 1.0f
);
[[nodiscard]] glm::vec4 point_light_bounds(const glm::vec2& position, float radius);
[[nodiscard]] int shadow_sample_count(ShadowMode mode);
[[nodiscard]] float shadow_penumbra_radius(
    const Light2DComponent& light,
    float caster_distance,
    float caster_height
);
[[nodiscard]] glm::vec4 blend_light_sample(
    const glm::vec4& base,
    const glm::vec4& light,
    float coverage,
    Light2DBlendMode mode
);

// Builds the polygon that is erased from a point light map. The result is a
// triangle list in world space and includes both the caster and its extrusion.
[[nodiscard]] std::vector<glm::vec2> build_shadow_triangles(
    const glm::vec2& light_position,
    const std::vector<glm::vec2>& polygon,
    float extrusion_distance
);
[[nodiscard]] std::vector<glm::vec2> build_directional_shadow_triangles(
    const glm::vec2& light_direction,
    const std::vector<glm::vec2>& polygon,
    float extrusion_distance
);

class Lighting2DPass final : public ILightingPass {
public:
    ~Lighting2DPass() override = default;

    [[nodiscard]] const char* name() const override { return "Lighting2D"; }
    [[nodiscard]] bool is_enabled() const override { return !m_lights.empty(); }
    [[nodiscard]] RenderLayerMask affected_layers() const override { return m_affected_layers; }
    [[nodiscard]] bool requires_shadow_receiver_mask() const override;
    bool apply(const LightingContext& context) override;
    void invalidate_resources() override {
        m_shadow_white.reset();
        m_shadow_white_renderer = nullptr;
    }

    void update(std::vector<Light2DInstance> lights, std::vector<ShadowPolygon2D> casters);
    void set_shadow_budget(int max_shadowed_lights) { m_max_shadowed_lights = std::clamp(max_shadowed_lights, 0, 16); }
    [[nodiscard]] const std::vector<Light2DInstance>& lights() const { return m_lights; }
    [[nodiscard]] const std::vector<ShadowPolygon2D>& casters() const { return m_casters; }

private:
    bool draw_point_gradient(
        SDL_Renderer* renderer,
        const glm::vec2& center,
        float radius,
        const Light2DComponent& light,
        float world_rotation
    );
    bool draw_area_gradient(
        SDL_Renderer* renderer,
        const LightingContext& context,
        const Light2DInstance& instance
    );
    std::vector<Light2DInstance> m_lights;
    std::vector<ShadowPolygon2D> m_casters;
    RenderLayerMask m_affected_layers{0};
    std::vector<SDL_Vertex> m_radial_vertices;
    std::vector<int> m_radial_indices;
    std::unique_ptr<Texture> m_shadow_white;
    SDL_Renderer* m_shadow_white_renderer{nullptr};
    int m_max_shadowed_lights{8};
};

// Extracts authored ECS data into a cached runtime pass. Scene integration is
// one generic configure call; light recipes remain entirely data-driven.
class Lighting2DSystem {
public:
    Lighting2DSystem();
    void configure(EntityManager& entities, Renderer2D& renderer);
    [[nodiscard]] const std::shared_ptr<Lighting2DPass>& runtime_pass() const { return m_runtime_pass; }

private:
    std::shared_ptr<Lighting2DPass> m_runtime_pass;
    ShadowSilhouetteCache m_silhouette_cache;
};

} // namespace Engine
