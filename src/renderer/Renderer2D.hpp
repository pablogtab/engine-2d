#pragma once

#include "Texture.hpp"
#include "../scene/Camera2D.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <SDL3/SDL.h>

namespace Engine {

struct SpriteCommand {
    Texture* texture{nullptr};
    glm::vec3 position{0.0f}; // X, Y, Z (Depth)
    glm::vec2 size{32.0f, 32.0f};
    float rotation{0.0f};
    SDL_Color color{255, 255, 255, 255};
    SDL_FRect src_rect{0.0f, 0.0f, 0.0f, 0.0f};
    bool has_src_rect{false};
};

class Renderer2D {
public:
    explicit Renderer2D(SDL_Renderer* renderer);

    void begin_scene(const Camera2D& camera);
    void submit_sprite(
        Texture* texture, 
        const glm::vec3& position, 
        const glm::vec2& size, 
        float rotation = 0.0f, 
        SDL_Color color = {255, 255, 255, 255},
        const SDL_FRect* src_rect = nullptr
    );
    void end_scene();

private:
    SDL_Renderer* m_sdl_renderer{nullptr};
    const Camera2D* m_active_camera{nullptr};
    std::vector<SpriteCommand> m_sprite_queue;
};

} // namespace Engine
