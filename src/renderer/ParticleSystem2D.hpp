#pragma once

#include "Renderer2D.hpp"
#include "../ecs/Components.hpp"
#include <vector>
#include <random>

namespace Engine {

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
    float bounciness{0.6f};

    Texture* texture{nullptr};
    bool active{false};
};

class ParticleSystem2D {
public:
    explicit ParticleSystem2D(size_t max_particles = 2500);

    void reserve_capacity(size_t max_particles);
    size_t get_capacity() const { return m_particle_pool.size(); }
    size_t get_active_count() const { return m_active_count; }

    void emit(const ParticleProps& props, int count = 1);
    void update(float delta_time);
    void render(Renderer2D& renderer);
    void clear();

    // High-level gameplay juice presets
    void burst_explosion(const glm::vec2& pos, int spark_count = 45, int smoke_count = 18);
    void burst_coin_sparkle(const glm::vec2& pos, int count = 16);
    void burst_grapple_sparks(const glm::vec2& pos, int count = 12);
    void burst_dust(const glm::vec2& pos, int count = 8);

private:
    float random_range(float min_val, float max_val);

    std::vector<Particle> m_particle_pool;
    size_t m_pool_index{0};
    size_t m_active_count{0};

    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist01{0.0f, 1.0f};
};

} // namespace Engine
