#include "ParticleSystem2D.hpp"
#include "../ecs/PhysicsSystem.hpp"
#include <cmath>

namespace Engine {

ParticleSystem2D::ParticleSystem2D(size_t max_particles)
    : m_rng(std::random_device{}()) {
    reserve_capacity(max_particles);
}

void ParticleSystem2D::reserve_capacity(size_t max_particles) {
    m_particle_pool.resize(max_particles);
    m_pool_index = 0;
    m_active_count = 0;
}

float ParticleSystem2D::random_range(float min_val, float max_val) {
    return min_val + (max_val - min_val) * m_dist01(m_rng);
}

void ParticleSystem2D::emit(const ParticleProps& props, int count) {
    if (m_particle_pool.empty()) return;

    for (int i = 0; i < count; ++i) {
        Particle& p = m_particle_pool[m_pool_index];
        p.active = true;
        p.position = props.position;
        p.rotation = props.rotation + random_range(-180.0f, 180.0f);
        p.rotation_speed = props.rotation_speed + random_range(-90.0f, 90.0f);
        p.texture = props.texture;
        p.collide_with_world = props.collide_with_world;
        p.bounciness = props.bounciness;

        p.velocity = props.velocity + glm::vec2(
            random_range(-props.velocity_variation.x, props.velocity_variation.x),
            random_range(-props.velocity_variation.y, props.velocity_variation.y)
        );
        p.acceleration = props.acceleration;
        p.gravity = props.gravity;

        p.color_start = props.color_start;
        p.color_end = props.color_end;
        p.current_color = props.color_start;

        float size_var = random_range(-props.size_variation, props.size_variation);
        p.size_start = std::max(1.0f, props.size_start + size_var);
        p.size_end = std::max(0.0f, props.size_end + size_var * 0.5f);
        p.current_size = p.size_start;

        float life_var = random_range(-props.lifetime_variation, props.lifetime_variation);
        p.lifetime = std::max(0.05f, props.lifetime + life_var);
        p.age = 0.0f;

        m_pool_index = (m_pool_index + 1) % m_particle_pool.size();
    }
}

void ParticleSystem2D::update(float delta_time) {
    m_active_count = 0;
    for (auto& p : m_particle_pool) {
        if (!p.active) continue;

        p.age += delta_time;
        if (p.age >= p.lifetime) {
            p.active = false;
            continue;
        }

        m_active_count++;

        // Kinematics & Solid Obstacle Collision Reflection
        glm::vec2 prev_pos = p.position;
        p.velocity += (p.acceleration + p.gravity) * delta_time;
        glm::vec2 next_pos = prev_pos + p.velocity * delta_time;

        if (p.collide_with_world) {
            auto hit = PhysicsSystem::raycast(prev_pos, next_pos, CollisionCategory::GROUND, true);
            if (hit.hit) {
                p.position = hit.point + hit.normal * 1.5f;
                // Perfect elastic/friction reflection: v' = reflect(v, n) * bounciness
                p.velocity = glm::reflect(p.velocity, hit.normal) * p.bounciness;
                p.rotation_speed *= 0.5f;
            } else {
                p.position = next_pos;
            }
        } else {
            p.position = next_pos;
        }

        p.rotation += p.rotation_speed * delta_time;

        float progress = p.age / p.lifetime;
        p.current_size = std::lerp(p.size_start, p.size_end, progress);

        p.current_color.r = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.r), static_cast<float>(p.color_end.r), progress));
        p.current_color.g = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.g), static_cast<float>(p.color_end.g), progress));
        p.current_color.b = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.b), static_cast<float>(p.color_end.b), progress));
        p.current_color.a = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.a), static_cast<float>(p.color_end.a), progress));
    }
}

void ParticleSystem2D::render(Renderer2D& renderer) {
    for (const auto& p : m_particle_pool) {
        if (!p.active || p.current_size <= 0.5f || p.current_color.a == 0) continue;

        renderer.submit_sprite(
            p.texture,
            glm::vec3(p.position, 5.0f), // Render above terrain/cables
            glm::vec2(p.current_size),
            p.rotation,
            p.current_color,
            nullptr,
            glm::vec2(0.5f, 0.5f),
            false
        );
    }
}

void ParticleSystem2D::clear() {
    for (auto& p : m_particle_pool) {
        p.active = false;
    }
    m_pool_index = 0;
    m_active_count = 0;
}

void ParticleSystem2D::burst_explosion(const glm::vec2& pos, int spark_count, int smoke_count) {
    // 1. Fiery Bouncing Sparks
    for (int i = 0; i < spark_count; ++i) {
        float angle = random_range(0.0f, 6.28318f);
        float speed = random_range(180.0f, 580.0f);

        ParticleProps spark;
        spark.position = pos;
        spark.velocity = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed);
        spark.gravity = glm::vec2(0.0f, -650.0f); // Fall with gravity
        spark.collide_with_world = true;          // BOUNCE on terrain/ramps!
        spark.bounciness = random_range(0.45f, 0.70f);
        spark.color_start = SDL_Color{255, 230, 60, 255};
        spark.color_end = SDL_Color{255, 50, 0, 0};
        spark.size_start = random_range(6.0f, 10.0f);
        spark.size_end = 1.0f;
        spark.lifetime = random_range(0.45f, 0.95f);
        spark.rotation_speed = random_range(-360.0f, 360.0f);
        emit(spark, 1);
    }

    // 2. Lingering Smoke Plumes
    for (int i = 0; i < smoke_count; ++i) {
        float angle = random_range(0.0f, 6.28318f);
        float speed = random_range(30.0f, 120.0f);

        ParticleProps smoke;
        smoke.position = pos + glm::vec2(random_range(-12.0f, 12.0f), random_range(-12.0f, 12.0f));
        smoke.velocity = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed + 35.0f); // Float upward
        smoke.gravity = glm::vec2(0.0f, 20.0f); // Slight upward buoyancy
        smoke.color_start = SDL_Color{70, 70, 75, 210};
        smoke.color_end = SDL_Color{25, 25, 30, 0};
        smoke.size_start = random_range(10.0f, 16.0f);
        smoke.size_end = random_range(28.0f, 42.0f); // Expands into billowy clouds
        smoke.lifetime = random_range(0.6f, 1.1f);
        smoke.rotation_speed = random_range(-60.0f, 60.0f);
        emit(smoke, 1);
    }
}

void ParticleSystem2D::burst_coin_sparkle(const glm::vec2& pos, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = random_range(0.0f, 6.28318f);
        float speed = random_range(90.0f, 260.0f);

        ParticleProps sparkle;
        sparkle.position = pos;
        sparkle.velocity = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed);
        sparkle.gravity = glm::vec2(0.0f, -120.0f);
        sparkle.color_start = SDL_Color{255, 255, 200, 255}; // Diamond flash
        sparkle.color_end = SDL_Color{255, 190, 0, 0};       // Fade to pure gold
        sparkle.size_start = random_range(7.0f, 11.0f);
        sparkle.size_end = 0.0f;
        sparkle.lifetime = random_range(0.35f, 0.65f);
        sparkle.rotation_speed = random_range(-240.0f, 240.0f);
        emit(sparkle, 1);
    }
}

void ParticleSystem2D::burst_grapple_sparks(const glm::vec2& pos, int count) {
    for (int i = 0; i < count; ++i) {
        float angle = random_range(-2.35f, -0.78f); // Downward ricochet spray [-135, -45 deg]
        float speed = random_range(120.0f, 320.0f);

        ParticleProps spark;
        spark.position = pos;
        spark.velocity = glm::vec2(std::cos(angle) * speed, std::sin(angle) * speed);
        spark.gravity = glm::vec2(0.0f, -700.0f);
        spark.color_start = SDL_Color{255, 240, 120, 255};
        spark.color_end = SDL_Color{255, 100, 0, 0};
        spark.size_start = random_range(4.0f, 7.0f);
        spark.size_end = 0.5f;
        spark.lifetime = random_range(0.2f, 0.45f);
        emit(spark, 1);
    }
}

void ParticleSystem2D::burst_dust(const glm::vec2& pos, int count) {
    for (int i = 0; i < count; ++i) {
        float dir_x = random_range(-1.0f, 1.0f);
        float speed_x = dir_x * random_range(40.0f, 110.0f);
        float speed_y = random_range(15.0f, 55.0f);

        ParticleProps dust;
        dust.position = pos + glm::vec2(random_range(-8.0f, 8.0f), 0.0f);
        dust.velocity = glm::vec2(speed_x, speed_y);
        dust.gravity = glm::vec2(0.0f, -30.0f);
        dust.color_start = SDL_Color{210, 200, 185, 190};
        dust.color_end = SDL_Color{160, 150, 140, 0};
        dust.size_start = random_range(5.0f, 8.0f);
        dust.size_end = random_range(12.0f, 18.0f);
        dust.lifetime = random_range(0.25f, 0.45f);
        emit(dust, 1);
    }
}

} // namespace Engine
