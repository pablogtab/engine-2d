#include "ParticleSystem2D.hpp"
#include "../physics/PhysicsSystem.hpp"
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

void ParticleSystem2D::init_default_textures(SDL_Renderer* renderer) {
    if (!m_default_soft_circle) {
        m_default_soft_circle = std::unique_ptr<Texture>(Texture::create_soft_circle(renderer, 16));
    }
    if (!m_default_spark) {
        m_default_spark = std::unique_ptr<Texture>(Texture::create_spark(renderer, 16));
    }
}

void ParticleSystem2D::emit(const ParticleProps& props, int count) {
    if (m_particle_pool.empty()) return;

    Texture* tex = props.texture ? props.texture : m_default_soft_circle.get();

    for (int i = 0; i < count; ++i) {
        Particle& p = m_particle_pool[m_pool_index];
        p.active = true;
        p.position = props.position;
        p.rotation = props.rotation + random_range(-180.0f, 180.0f);
        p.rotation_speed = props.rotation_speed + random_range(-90.0f, 90.0f);
        p.texture = tex;
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

} // namespace Engine
