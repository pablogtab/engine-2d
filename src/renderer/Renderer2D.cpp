#include "Renderer2D.hpp"
#include <algorithm>

namespace Engine {

Renderer2D::Renderer2D(SDL_Renderer* renderer)
    : m_sdl_renderer(renderer) {}

void Renderer2D::begin_scene(const Camera2D& camera) {
    m_active_camera = &camera;
    m_sprite_queue.clear();
}

void Renderer2D::submit_sprite(
    Texture* texture, 
    const glm::vec3& position, 
    const glm::vec2& size, 
    float rotation, 
    SDL_Color color,
    const SDL_FRect* src_rect
) {
    SpriteCommand cmd{texture, position, size, rotation, color};
    if (src_rect) {
        cmd.src_rect = *src_rect;
        cmd.has_src_rect = true;
    }
    m_sprite_queue.push_back(cmd);
}

void Renderer2D::end_scene() {
    if (!m_active_camera || !m_sdl_renderer) return;

    // Depth sorting (Z-depth first, then Y-sorting)
    std::sort(m_sprite_queue.begin(), m_sprite_queue.end(), [](const SpriteCommand& a, const SpriteCommand& b) {
        if (a.position.z != b.position.z) {
            return a.position.z < b.position.z; // Smaller Z goes to background
        }
        return a.position.y < b.position.y;     // Smaller Y goes behind on Z=0 plane (Top-down Y-sorting)
    });

    // Render sorted sprite queue
    for (const auto& cmd : m_sprite_queue) {
        if (!cmd.texture) continue;

        // Calculate GPU screen position based on camera parallax and Z-depth
        glm::vec2 screen_pos = m_active_camera->world_to_screen(cmd.position);
        float z_scale = m_active_camera->get_z_scale(cmd.position.z) * m_active_camera->get_zoom();

        glm::vec2 scaled_size = cmd.size * z_scale;

        SDL_FRect dest_rect{
            screen_pos.x - scaled_size.x * 0.5f,
            screen_pos.y - scaled_size.y * 0.5f,
            scaled_size.x,
            scaled_size.y
        };

        SDL_SetTextureColorMod(cmd.texture->get_sdl_texture(), cmd.color.r, cmd.color.g, cmd.color.b);
        SDL_SetTextureAlphaMod(cmd.texture->get_sdl_texture(), cmd.color.a);

        const SDL_FRect* p_src = cmd.has_src_rect ? &cmd.src_rect : nullptr;

        if (cmd.rotation != 0.0f) {
            SDL_RenderTextureRotated(
                m_sdl_renderer, 
                cmd.texture->get_sdl_texture(), 
                p_src, 
                &dest_rect, 
                cmd.rotation, 
                nullptr, 
                SDL_FLIP_NONE
            );
        } else {
            SDL_RenderTexture(
                m_sdl_renderer, 
                cmd.texture->get_sdl_texture(), 
                p_src, 
                &dest_rect
            );
        }
    }
}

} // namespace Engine
