#include "ParticleWorld.hpp"
#include "../core/AssetManager.hpp"
#include "../core/Log.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include <algorithm>

namespace Engine {

ParticleWorld::ParticleWorld(size_t max_particles)
    : m_particle_system(max_particles) {
}

void ParticleWorld::set_renderer(SDL_Renderer* renderer) {
    if (m_renderer == renderer) return;
    m_renderer = renderer;
    if (m_renderer) m_particle_system.init_default_textures(m_renderer);
}

void ParticleWorld::invalidate_render_resources() {
    m_particle_system.invalidate_render_resources();
}

const ParticleEffectAsset* ParticleWorld::load_effect(
    AssetHandle handle,
    const AssetDatabase& database
) {
    if (!handle.is_valid()) return nullptr;
    if (m_failed_effects.contains(handle.value)) return nullptr;
    auto cached = m_asset_cache.find(handle.value);
    if (cached != m_asset_cache.end()) return &cached->second;
    const AssetRecord* record = database.find(handle);
    if (!record || record->status != AssetStatus::Ready || record->type != AssetType::ParticleEffect) {
        m_failed_effects.insert(handle.value);
        return nullptr;
    }
    std::string error;
    auto loaded = ParticleEffectAsset::load_from_file(database.resolve(handle), &error);
    if (!loaded) {
        m_failed_effects.insert(handle.value);
        ENGINE_LOG_ERROR("[PARTICLES] Failed loading '{}': {}", record->source_path, error);
        return nullptr;
    }
    return &m_asset_cache.emplace(handle.value, std::move(*loaded)).first->second;
}

Texture* ParticleWorld::resolve_texture(
    const ParticleEffectAsset& asset,
    const AssetDatabase& database
) {
    if (!asset.texture_handle.is_valid() || !m_renderer) return nullptr;
    const AssetRecord* texture = database.find(asset.texture_handle);
    if (!texture || texture->status != AssetStatus::Ready || texture->type != AssetType::Texture) return nullptr;
    const std::string path = database.resolve(asset.texture_handle);
    return path.empty() ? nullptr : AssetManager::get().get_texture(m_renderer, path);
}

int ParticleWorld::clamp_emission(int requested) {
    if (requested <= 0) return 0;
    const std::size_t available = DEFAULT_FRAME_EMISSION_LIMIT > m_emitted_this_frame
        ? DEFAULT_FRAME_EMISSION_LIMIT - m_emitted_this_frame : 0;
    const int accepted = std::min<int>(requested, static_cast<int>(available));
    m_dropped_this_frame += static_cast<std::size_t>(requested - accepted);
    return accepted;
}

void ParticleWorld::begin_frame() {
    m_emitted_this_frame = 0;
    m_dropped_this_frame = 0;
    m_active_emitter_count = 0;
    m_particle_system.reset_dropped_count();
}

void ParticleWorld::update(Scene& scene, float delta_time, const AssetDatabase& database) {
    if (m_renderer) m_particle_system.init_default_textures(m_renderer);
    auto& em = scene.get_entity_manager();

    em.for_each<TransformComponent, ParticleEmitter2DComponent>([&](
        EntityID entity,
        const TransformComponent& transform,
        ParticleEmitter2DComponent& emitter
    ) {
        if (!emitter.active || !emitter.is_playing) return;
        ++m_active_emitter_count;

        if (emitter.delay_remaining > 0.0f) {
            emitter.delay_remaining -= delta_time;
            if (emitter.delay_remaining > 0.0f) return;
        }

        if (const ParticleEffectAsset* asset_ptr = load_effect(emitter.effect_handle, database)) {
                const auto& asset = *asset_ptr;

                // Duration and loop handling
                if (asset.emission.duration > 0.0f) {
                    emitter.lifetime_timer += delta_time;
                    if (emitter.lifetime_timer >= asset.emission.duration) {
                        if (asset.emission.loop) {
                            emitter.lifetime_timer = 0.0f;
                            emitter.delay_remaining = asset.emission.start_delay;
                            emitter.emission_accumulator = 0.0f;
                            emitter.distance_emission_accumulator = 0.0f;
                            emitter.has_previous_emitter_position = false;
                            return;
                        } else {
                            emitter.is_playing = false;
                            return;
                        }
                    }
                }

                const float rate = emitter.emit_rate_override.value_or(asset.emission.continuous_rate);
                const glm::vec2 spawn_pos(transform.world_position.x, transform.world_position.y);
                if (rate > 0.0f) {
                    emitter.emission_accumulator += delta_time * rate;
                    const int requested = static_cast<int>(emitter.emission_accumulator);
                    emitter.emission_accumulator -= static_cast<float>(requested);
                    int emit_count = clamp_emission(requested);
                    if (emit_count > 0) {
                        m_emitted_this_frame += m_particle_system.emit(
                            asset,
                            spawn_pos,
                            transform.world_rotation,
                            emit_count,
                            resolve_texture(asset, database),
                            entity,
                            emitter.local_space,
                            DEFAULT_EMITTER_LIMIT
                        );
                    }
                }

                if (!emitter.has_previous_emitter_position) {
                    emitter.previous_emitter_position = spawn_pos;
                    emitter.has_previous_emitter_position = true;
                }
                const glm::vec2 movement = spawn_pos - emitter.previous_emitter_position;
                const float travelled = glm::length(movement);
                if (asset.emission.distance_rate > 0.0f && travelled > 0.0001f) {
                    const float previous_fraction = emitter.distance_emission_accumulator;
                    const float generated = previous_fraction + travelled * asset.emission.distance_rate;
                    const int requested = static_cast<int>(generated);
                    emitter.distance_emission_accumulator = generated - static_cast<float>(requested);
                    const int emit_count = clamp_emission(requested);
                    if (emit_count > 0) {
                        const glm::vec2 direction = movement / travelled;
                        const float spacing = 1.0f / asset.emission.distance_rate;
                        const float first_distance = (1.0f - previous_fraction) * spacing;
                        Texture* texture = resolve_texture(asset, database);
                        for (int index = 0; index < emit_count; ++index) {
                            const float along = std::min(
                                first_distance + static_cast<float>(index) * spacing, travelled
                            );
                            m_emitted_this_frame += m_particle_system.emit(
                                asset,
                                emitter.previous_emitter_position + direction * along,
                                transform.world_rotation,
                                1,
                                texture,
                                entity,
                                false,
                                DEFAULT_EMITTER_LIMIT
                            );
                        }
                    }
                }
                emitter.previous_emitter_position = spawn_pos;
        }
    });

    m_particle_system.clear_orphaned_local_particles(em);
    m_particle_system.update(delta_time);
    m_dropped_this_frame += m_particle_system.get_dropped_count();
}

void ParticleWorld::render(Renderer2D& renderer, const EntityManager& entities) {
    m_particle_system.render(renderer, &entities);
}

void ParticleWorld::clear() {
    m_particle_system.clear();
    m_asset_cache.clear();
    m_failed_effects.clear();
}

bool ParticleWorld::emit(
    const ParticleEffectAsset& asset,
    const glm::vec2& position,
    float rotation,
    int count,
    Texture* texture
) {
    const int accepted = clamp_emission(count);
    if (accepted == 0) return false;
    const std::size_t emitted = m_particle_system.emit(
        asset, position, rotation, accepted, texture, 0, false, DEFAULT_EMITTER_LIMIT
    );
    m_emitted_this_frame += emitted;
    return emitted > 0;
}

bool ParticleWorld::emit_asset(
    AssetHandle handle,
    const glm::vec2& position,
    const AssetDatabase& database,
    int count
) {
    const ParticleEffectAsset* asset = load_effect(handle, database);
    return asset && emit(*asset, position, 0.0f, count, resolve_texture(*asset, database));
}

bool ParticleWorld::emit_asset(
    AssetHandle handle,
    const glm::vec2& position,
    const AssetDatabase& database,
    const ParticleEmissionOverrides& overrides
) {
    const ParticleEffectAsset* source = load_effect(handle, database);
    if (!source || overrides.count <= 0 || !std::isfinite(overrides.scale) || overrides.scale <= 0.0f) {
        return false;
    }
    ParticleEffectAsset effect = *source;
    if (overrides.velocity) effect.motion.initial_velocity = *overrides.velocity;
    if (overrides.velocity_variation) effect.motion.velocity_variation = *overrides.velocity_variation;
    if (overrides.color) effect.appearance.start_color = *overrides.color;
    effect.appearance.start_size *= overrides.scale;
    effect.appearance.end_size *= overrides.scale;
    effect.appearance.size_variation *= overrides.scale;
    return emit(
        effect,
        position,
        overrides.rotation,
        overrides.count,
        resolve_texture(effect, database)
    );
}

bool ParticleWorld::burst_entity(
    EntityID entity,
    Scene& scene,
    const AssetDatabase& database,
    int count_override
) {
    auto& em = scene.get_entity_manager();
    if (!em.is_alive(entity) || !em.has_component<ParticleEmitter2DComponent>(entity) || !em.has_component<TransformComponent>(entity)) {
        return false;
    }

    const auto& transform = em.get_component<TransformComponent>(entity);
    const auto& emitter = em.get_component<ParticleEmitter2DComponent>(entity);
    if (!emitter.active) return false;
    const glm::vec2 spawn_pos(transform.world_position.x, transform.world_position.y);

    if (const ParticleEffectAsset* asset_ptr = load_effect(emitter.effect_handle, database)) {
            const auto& asset = *asset_ptr;
            int count = count_override > 0 ? count_override
                      : emitter.burst_count_override.value_or(asset.emission.burst_count);
            count = clamp_emission(count);
            if (count <= 0) return false;
            const std::size_t emitted = m_particle_system.emit(
                asset,
                spawn_pos,
                transform.world_rotation,
                count,
                resolve_texture(asset, database),
                entity,
                emitter.local_space,
                DEFAULT_EMITTER_LIMIT
            );
            m_emitted_this_frame += emitted;
            return emitted > 0;
    }
    return false;
}

bool ParticleWorld::play_entity(EntityID entity, Scene& scene, const AssetDatabase& database) {
    auto& em = scene.get_entity_manager();
    if (!em.is_alive(entity) || !em.has_component<ParticleEmitter2DComponent>(entity)) return false;

    auto& emitter = em.get_component<ParticleEmitter2DComponent>(entity);
    emitter.is_playing = true;
    emitter.lifetime_timer = 0.0f;
    emitter.emission_accumulator = 0.0f;
    emitter.distance_emission_accumulator = 0.0f;
    emitter.has_previous_emitter_position = false;

    const ParticleEffectAsset* asset = load_effect(emitter.effect_handle, database);
    if (!asset) {
        emitter.is_playing = false;
        return false;
    }
    emitter.delay_remaining = asset->emission.start_delay;
    return true;
}

bool ParticleWorld::stop_entity(EntityID entity, Scene& scene, bool clear_particles) {
    auto& em = scene.get_entity_manager();
    if (!em.is_alive(entity) || !em.has_component<ParticleEmitter2DComponent>(entity)) return false;

    auto& emitter = em.get_component<ParticleEmitter2DComponent>(entity);
    emitter.is_playing = false;
    emitter.lifetime_timer = 0.0f;
    emitter.emission_accumulator = 0.0f;
    emitter.distance_emission_accumulator = 0.0f;
    emitter.has_previous_emitter_position = false;
    if (clear_particles) m_particle_system.clear_emitter(entity);
    return true;
}

} // namespace Engine
