#pragma once

#include "../../renderer/Texture.hpp"
#include "../../renderer/RenderLayer.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>

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
    ParticleProps props;
    float emit_rate{30.0f};
    float emit_timer{0.0f};
    bool active{true};
    int burst_count{1};

    ParticleEmitter2DComponent() = default;
    explicit ParticleEmitter2DComponent(
        const ParticleProps& properties,
        float rate = 30.0f,
        bool start_active = true
    ) : props(properties), emit_rate(rate), active(start_active) {}
};

} // namespace Engine
