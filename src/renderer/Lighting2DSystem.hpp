#pragma once

#include "LightingPass.hpp"
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
};

[[nodiscard]] float point_light_falloff(
    float distance,
    float radius,
    float intensity,
    float exponent = 1.0f
);
[[nodiscard]] glm::vec4 point_light_bounds(const glm::vec2& position, float radius);
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
    bool apply(const LightingContext& context) override;
    void invalidate_resources() override {}

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
    std::vector<Light2DInstance> m_lights;
    std::vector<ShadowPolygon2D> m_casters;
    RenderLayerMask m_affected_layers{0};
    std::vector<SDL_Vertex> m_radial_vertices;
    std::vector<int> m_radial_indices;
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
};

} // namespace Engine
