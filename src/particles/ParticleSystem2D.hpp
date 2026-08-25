#pragma once

#include "../renderer/Renderer2D.hpp"
#include "../renderer/Texture.hpp"
#include "../ecs/components/ParticleComponents.hpp"
#include "../ecs/EcsTypes.hpp"
#include "ParticleEffectAsset.hpp"
#include <vector>
#include <random>
#include <memory>
#include <optional>

namespace Engine {

class EntityManager;

struct Particle {
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 velocity{0.0f, 0.0f};
    glm::vec2 acceleration{0.0f, 0.0f};
    glm::vec2 gravity{0.0f, 0.0f};

    SDL_Color color_start{255, 255, 255, 255};
    SDL_Color color_end{255, 255, 255, 0};
    SDL_Color current_color{255, 255, 255, 255};

    float size_start{8.0f};
    float size_end{0.0f};
    float current_size{8.0f};

    float lifetime{1.0f};
    float age{0.0f};

    float rotation{0.0f};
    float rotation_speed{0.0f};

    bool collide_with_world{false};
    uint16_t collision_mask{static_cast<uint16_t>(CollisionCategory::GROUND)};
    float bounciness{0.6f};

    Texture* texture{nullptr};
    float z_layer{-0.05f};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
    ParticleBlendMode blend_mode{ParticleBlendMode::Alpha};
    EntityID source_emitter{0};
    bool local_space{false};
    bool active{false};
};

class ParticleSystem2D {
public:
    explicit ParticleSystem2D(size_t max_particles = 2500, std::optional<uint32_t> seed = std::nullopt);

    void set_seed(uint32_t seed);
    void reserve_capacity(size_t max_particles);
    size_t get_capacity() const { return m_particle_pool.size(); }
    size_t get_active_count() const {
        size_t count = 0;
        for (const auto& p : m_particle_pool) {
            if (p.active) count++;
        }
        return count;
    }

    [[nodiscard]] std::size_t emit(const ParticleProps& props, int count = 1);
    [[nodiscard]] std::size_t emit(
        const ParticleEffectAsset& asset,
        const glm::vec2& origin_position,
        float origin_rotation = 0.0f,
        int count = 1,
        Texture* texture_override = nullptr,
        EntityID source_emitter = 0,
        bool local_space = false,
        std::size_t emitter_limit = 500
    );
    void update(float delta_time);
    void render(Renderer2D& renderer, const EntityManager* entities = nullptr);
    void clear();
    void clear_emitter(EntityID entity);
    void clear_orphaned_local_particles(const EntityManager& entities);
    [[nodiscard]] std::size_t get_emitter_active_count(EntityID entity) const;
    [[nodiscard]] std::size_t get_dropped_count() const { return m_dropped_count; }
    void reset_dropped_count() { m_dropped_count = 0; }

    void init_default_textures(SDL_Renderer* renderer);
    void invalidate_render_resources();

    Texture* get_default_soft_circle() const { return m_default_soft_circle.get(); }
    Texture* get_default_spark() const { return m_default_spark.get(); }

private:
    Particle* acquire(EntityID source_emitter, std::size_t emitter_limit);
    float random_range(float min_val, float max_val);

    std::unique_ptr<Texture> m_default_soft_circle;
    std::unique_ptr<Texture> m_default_spark;

    std::vector<Particle> m_particle_pool;
    size_t m_pool_index{0};
    size_t m_active_count{0};
    size_t m_dropped_count{0};

    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist01{0.0f, 1.0f};
};

} // namespace Engine
