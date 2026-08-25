#pragma once

#include "ParticleSystem2D.hpp"
#include "ParticleEffectAsset.hpp"
#include "../scene/Scene.hpp"
#include "../core/AssetDatabase.hpp"
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <optional>

namespace Engine {

struct ParticleEmissionOverrides {
    int count{1};
    std::optional<glm::vec2> velocity;
    std::optional<glm::vec2> velocity_variation;
    std::optional<SDL_Color> color;
    float scale{1.0f};
    float rotation{0.0f};
};

class ParticleWorld {
public:
    static constexpr std::size_t DEFAULT_GLOBAL_LIMIT = 2500;
    static constexpr std::size_t DEFAULT_EMITTER_LIMIT = 500;
    static constexpr std::size_t DEFAULT_FRAME_EMISSION_LIMIT = 250;

    explicit ParticleWorld(size_t max_particles = 2500);

    void set_renderer(SDL_Renderer* renderer);
    void invalidate_render_resources();
    void begin_frame();
    void update(Scene& scene, float delta_time, const AssetDatabase& database);
    void render(Renderer2D& renderer, const EntityManager& entities);
    void clear();

    [[nodiscard]] bool emit(
        const ParticleEffectAsset& asset,
        const glm::vec2& position,
        float rotation = 0.0f,
        int count = 1,
        Texture* texture = nullptr
    );
    [[nodiscard]] bool emit_asset(
        AssetHandle handle,
        const glm::vec2& position,
        const AssetDatabase& database,
        int count = 1
    );
    [[nodiscard]] bool emit_asset(
        AssetHandle handle,
        const glm::vec2& position,
        const AssetDatabase& database,
        const ParticleEmissionOverrides& overrides
    );

    [[nodiscard]] bool burst_entity(EntityID entity, Scene& scene, const AssetDatabase& database, int count_override = 0);
    [[nodiscard]] bool play_entity(EntityID entity, Scene& scene, const AssetDatabase& database);
    [[nodiscard]] bool stop_entity(EntityID entity, Scene& scene, bool clear_particles = false);

    [[nodiscard]] ParticleSystem2D& system() { return m_particle_system; }
    [[nodiscard]] const ParticleSystem2D& system() const { return m_particle_system; }
    [[nodiscard]] std::size_t emitted_this_frame() const { return m_emitted_this_frame; }
    [[nodiscard]] std::size_t dropped_this_frame() const { return m_dropped_this_frame; }
    [[nodiscard]] std::size_t active_emitter_count() const { return m_active_emitter_count; }

private:
    [[nodiscard]] const ParticleEffectAsset* load_effect(AssetHandle handle, const AssetDatabase& database);
    [[nodiscard]] Texture* resolve_texture(const ParticleEffectAsset& asset, const AssetDatabase& database);
    [[nodiscard]] int clamp_emission(int requested);

    ParticleSystem2D m_particle_system;
    std::unordered_map<uint64_t, ParticleEffectAsset> m_asset_cache;
    std::unordered_set<uint64_t> m_failed_effects;
    SDL_Renderer* m_renderer{nullptr};
    std::size_t m_emitted_this_frame{0};
    std::size_t m_dropped_this_frame{0};
    std::size_t m_active_emitter_count{0};
};

} // namespace Engine
