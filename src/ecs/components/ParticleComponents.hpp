#pragma once

#include "../../core/AssetHandle.hpp"
#include "../../renderer/Texture.hpp"
#include "../../renderer/RenderLayer.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <optional>

namespace Engine {

struct ParticleProps {
    glm::vec2 position{0.0f, 0.0f};
    glm::vec2 velocity{0.0f, 0.0f};
    glm::vec2 velocity_variation{0.0f, 0.0f};
    glm::vec2 acceleration{0.0f, 0.0f};
    glm::vec2 gravity{0.0f, 0.0f};
    SDL_Color color_start{255, 255, 255, 255};
    SDL_Color color_end{255, 255, 255, 0};
    float size_start{8.0f};
    float size_end{0.0f};
    float size_variation{0.0f};
    float lifetime{1.0f};
    float lifetime_variation{0.0f};
    float rotation{0.0f};
    float rotation_speed{0.0f};
    bool collide_with_world{false};
    float bounciness{0.6f};
    Texture* texture{nullptr};
    float z_layer{-0.05f};
    RenderLayer render_layer{DEFAULT_RENDER_LAYER};
};

struct ParticleEmitter2DComponent {
    AssetHandle effect_handle{};
    bool active{true};
    bool play_on_awake{true};
    bool local_space{false};
    std::optional<float> emit_rate_override{};
    std::optional<int> burst_count_override{};

    float emission_accumulator{0.0f};
    float lifetime_timer{0.0f};
    float delay_remaining{0.0f};
    bool is_playing{false};

    ParticleEmitter2DComponent() = default;

    bool operator==(const ParticleEmitter2DComponent& other) const {
        return effect_handle == other.effect_handle &&
               active == other.active &&
               play_on_awake == other.play_on_awake &&
               local_space == other.local_space &&
               emit_rate_override == other.emit_rate_override &&
               burst_count_override == other.burst_count_override;
    }
};

} // namespace Engine
