#pragma once

#include "LightingPass.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace Engine {

class EntityManager;
class Renderer2D;

struct Light2DInstance {
    glm::vec2 world_position{0.0f};
    Light2DComponent light;
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

class Lighting2DPass final : public ILightingPass {
public:
    ~Lighting2DPass() override;

    [[nodiscard]] const char* name() const override { return "Lighting2D"; }
    [[nodiscard]] bool is_enabled() const override { return !m_lights.empty(); }
    [[nodiscard]] RenderLayerMask affected_layers() const override { return m_affected_layers; }
    bool apply(const LightingContext& context) override;

    void update(std::vector<Light2DInstance> lights, std::vector<ShadowPolygon2D> casters);
    [[nodiscard]] const std::vector<Light2DInstance>& lights() const { return m_lights; }
    [[nodiscard]] const std::vector<ShadowPolygon2D>& casters() const { return m_casters; }

private:
    bool ensure_resources(SDL_Renderer* renderer, int width, int height);
    bool draw_point_gradient(
        SDL_Renderer* renderer,
        const glm::vec2& center,
        float radius,
        const Light2DComponent& light
    );
    void release_resources();

    std::vector<Light2DInstance> m_lights;
    std::vector<ShadowPolygon2D> m_casters;
    RenderLayerMask m_affected_layers{0};
    SDL_Renderer* m_resource_renderer{nullptr};
    SDL_Texture* m_light_map{nullptr};
    int m_light_map_width{0};
    int m_light_map_height{0};
    std::vector<SDL_Vertex> m_radial_vertices;
    std::vector<int> m_radial_indices;
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
