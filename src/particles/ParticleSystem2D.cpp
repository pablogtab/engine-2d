#include "ParticleSystem2D.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../physics/PhysicsSystem.hpp"
#include <cmath>

namespace Engine {

ParticleSystem2D::ParticleSystem2D(size_t max_particles, std::optional<uint32_t> seed)
    : m_rng(seed.has_value() ? std::mt19937(*seed) : std::mt19937(std::random_device{}())) {
    reserve_capacity(max_particles);
}

void ParticleSystem2D::set_seed(uint32_t seed) {
    m_rng.seed(seed);
}

void ParticleSystem2D::reserve_capacity(size_t max_particles) {
    m_particle_pool.resize(max_particles);
    m_pool_index = 0;
    m_active_count = 0;
}

float ParticleSystem2D::random_range(float min_val, float max_val) {
    if (min_val >= max_val) return min_val;
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

void ParticleSystem2D::invalidate_render_resources() {
    m_default_soft_circle.reset();
    m_default_spark.reset();
    for (Particle& particle : m_particle_pool) particle.texture = nullptr;
}

Particle* ParticleSystem2D::acquire(EntityID source_emitter, std::size_t emitter_limit) {
    if (m_particle_pool.empty() ||
        (source_emitter != 0 && get_emitter_active_count(source_emitter) >= emitter_limit)) {
        ++m_dropped_count;
        return nullptr;
    }
    for (std::size_t offset = 0; offset < m_particle_pool.size(); ++offset) {
        const std::size_t index = (m_pool_index + offset) % m_particle_pool.size();
        if (!m_particle_pool[index].active) {
            m_pool_index = (index + 1) % m_particle_pool.size();
            return &m_particle_pool[index];
        }
    }
    ++m_dropped_count;
    return nullptr;
}

std::size_t ParticleSystem2D::emit(const ParticleProps& props, int count) {
    if (m_particle_pool.empty() || count <= 0) return 0;

    Texture* tex = props.texture ? props.texture : m_default_soft_circle.get();
    std::size_t emitted = 0;

    for (int i = 0; i < count; ++i) {
        Particle* acquired = acquire(0, m_particle_pool.size());
        if (!acquired) continue;
        Particle& p = *acquired;
        p.active = true;
        p.source_emitter = 0;
        p.local_space = false;
        p.position = props.position;
        p.z_layer = props.z_layer;
        p.render_layer = props.render_layer;
        p.rotation = props.rotation;
        p.rotation_speed = props.rotation_speed;
        p.texture = tex;
        p.collide_with_world = props.collide_with_world;
        p.collision_mask = static_cast<uint16_t>(CollisionCategory::GROUND);
        p.blend_mode = ParticleBlendMode::Alpha;
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

        ++emitted;
    }
    return emitted;
}

std::size_t ParticleSystem2D::emit(
    const ParticleEffectAsset& asset,
    const glm::vec2& origin_position,
    float origin_rotation,
    int count,
    Texture* texture_override,
    EntityID source_emitter,
    bool local_space,
    std::size_t emitter_limit
) {
    if (m_particle_pool.empty() || count <= 0) return 0;

    Texture* tex = texture_override ? texture_override : m_default_soft_circle.get();
    std::size_t emitted = 0;

    for (int i = 0; i < count; ++i) {
        Particle* acquired = acquire(source_emitter, emitter_limit);
        if (!acquired) continue;
        Particle& p = *acquired;
        p.active = true;
        p.source_emitter = source_emitter;
        p.local_space = local_space && !asset.collision.enabled;
        p.position = p.local_space ? glm::vec2(0.0f) : origin_position;
        p.z_layer = asset.rendering.z_layer;
        p.render_layer = asset.rendering.render_layer;

        // Controlled rotation with optional authoring variation (no hardcoded jitter)
        float rot_var = asset.motion.rotation_variation > 0.0f
            ? random_range(-asset.motion.rotation_variation, asset.motion.rotation_variation) : 0.0f;
        p.rotation = (p.local_space ? 0.0f : origin_rotation) + asset.motion.initial_rotation + rot_var;

        float rot_speed_var = asset.motion.rotation_speed_variation > 0.0f
            ? random_range(-asset.motion.rotation_speed_variation, asset.motion.rotation_speed_variation) : 0.0f;
        p.rotation_speed = asset.motion.rotation_speed + rot_speed_var;

        p.texture = tex;
        p.collide_with_world = asset.collision.enabled;
        p.collision_mask = asset.collision.mask_bits;
        p.blend_mode = asset.rendering.blend_mode;
        p.bounciness = asset.collision.bounciness;

        // Velocity with optional orientation
        glm::vec2 base_vel = asset.motion.initial_velocity;
        if (!p.local_space && origin_rotation != 0.0f) {
            float rad = glm::radians(origin_rotation);
            float cos_r = std::cos(rad);
            float sin_r = std::sin(rad);
            base_vel = glm::vec2(base_vel.x * cos_r - base_vel.y * sin_r, base_vel.x * sin_r + base_vel.y * cos_r);
        }

        glm::vec2 vel_var(
            asset.motion.velocity_variation.x > 0.0f ? random_range(-asset.motion.velocity_variation.x, asset.motion.velocity_variation.x) : 0.0f,
            asset.motion.velocity_variation.y > 0.0f ? random_range(-asset.motion.velocity_variation.y, asset.motion.velocity_variation.y) : 0.0f
        );
        p.velocity = base_vel + vel_var;
        p.acceleration = asset.motion.acceleration;
        p.gravity = asset.motion.gravity;

        p.color_start = asset.appearance.start_color;
        p.color_end = asset.appearance.end_color;
        p.current_color = asset.appearance.start_color;

        float size_var = asset.appearance.size_variation > 0.0f
            ? random_range(-asset.appearance.size_variation, asset.appearance.size_variation) : 0.0f;
        p.size_start = std::max(0.1f, asset.appearance.start_size + size_var);
        p.size_end = std::max(0.0f, asset.appearance.end_size);
        p.current_size = p.size_start;

        float life_var = asset.lifetime.variation > 0.0f
            ? random_range(-asset.lifetime.variation, asset.lifetime.variation) : 0.0f;
        p.lifetime = std::max(0.01f, asset.lifetime.duration + life_var);
        p.age = 0.0f;

        ++emitted;
    }
    return emitted;
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
            auto hit = PhysicsSystem::raycast(prev_pos, next_pos, p.collision_mask, true);
            if (hit.hit) {
                p.position = hit.point + hit.normal * 1.5f;
                p.velocity = glm::reflect(p.velocity, hit.normal) * p.bounciness;
                p.rotation_speed *= 0.5f;
            } else {
                p.position = next_pos;
            }
        } else {
            p.position = next_pos;
        }

        p.rotation += p.rotation_speed * delta_time;

        float progress = p.lifetime > 0.0f ? std::clamp(p.age / p.lifetime, 0.0f, 1.0f) : 1.0f;
        p.current_size = std::lerp(p.size_start, p.size_end, progress);

        p.current_color.r = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.r), static_cast<float>(p.color_end.r), progress));
        p.current_color.g = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.g), static_cast<float>(p.color_end.g), progress));
        p.current_color.b = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.b), static_cast<float>(p.color_end.b), progress));
        p.current_color.a = static_cast<uint8_t>(std::lerp(static_cast<float>(p.color_start.a), static_cast<float>(p.color_end.a), progress));
    }
}

void ParticleSystem2D::render(Renderer2D& renderer, const EntityManager* entities) {
    for (const auto& p : m_particle_pool) {
        if (!p.active || p.current_size <= 0.5f || p.current_color.a == 0) continue;
        glm::vec2 render_position = p.position;
        float render_rotation = p.rotation;
        if (p.local_space) {
            if (!entities || !entities->is_alive(p.source_emitter) ||
                !entities->has_component<TransformComponent>(p.source_emitter)) continue;
            const TransformComponent& transform = entities->get_component<TransformComponent>(p.source_emitter);
            const float radians = glm::radians(transform.world_rotation);
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            render_position = glm::vec2(transform.world_position) + glm::vec2(
                p.position.x * cosine - p.position.y * sine,
                p.position.x * sine + p.position.y * cosine
            );
            render_rotation += transform.world_rotation;
        }

        renderer.submit_sprite(
            p.texture,
            glm::vec3(render_position, p.z_layer),
            glm::vec2(p.current_size),
            render_rotation,
            p.current_color,
            nullptr,
            glm::vec2(0.5f, 0.5f),
            false,
            false,
            false,
            0,
            0,
            nullptr,
            p.render_layer,
            {},
            0,
            p.blend_mode == ParticleBlendMode::Additive ? SDL_BLENDMODE_ADD : SDL_BLENDMODE_BLEND
        );
    }
}

void ParticleSystem2D::clear() {
    for (auto& p : m_particle_pool) {
        p.active = false;
    }
    m_pool_index = 0;
    m_active_count = 0;
    m_dropped_count = 0;
}

void ParticleSystem2D::clear_emitter(EntityID entity) {
    if (entity == 0) return;
    for (Particle& particle : m_particle_pool) {
        if (particle.active && particle.source_emitter == entity) particle.active = false;
    }
}

void ParticleSystem2D::clear_orphaned_local_particles(const EntityManager& entities) {
    for (Particle& particle : m_particle_pool) {
        if (particle.active && particle.local_space &&
            (!entities.is_alive(particle.source_emitter) ||
             !entities.has_component<TransformComponent>(particle.source_emitter))) {
            particle.active = false;
        }
    }
}

std::size_t ParticleSystem2D::get_emitter_active_count(EntityID entity) const {
    if (entity == 0) return 0;
    return static_cast<std::size_t>(std::count_if(
        m_particle_pool.begin(), m_particle_pool.end(),
        [entity](const Particle& particle) {
            return particle.active && particle.source_emitter == entity;
        }
    ));
}

} // namespace Engine
