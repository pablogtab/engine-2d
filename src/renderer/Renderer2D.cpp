#include "Renderer2D.hpp"
#include <algorithm>

namespace Engine {

Renderer2D::Renderer2D(SDL_Renderer* renderer)
    : m_sdl_renderer(renderer) {}

void Renderer2D::begin_scene(const Camera2D& camera) {
    m_active_camera = &camera;
    m_sprite_queue.clear();
    m_vertex_buffer.clear();
    m_index_buffer.clear();
    m_draw_calls = 0;
    m_rendered_sprites = 0;
    m_culled_sprites = 0;
}

void Renderer2D::submit_sprite(
    Texture* texture, 
    const glm::vec3& position, 
    const glm::vec2& size, 
    float rotation, 
    SDL_Color color,
    const SDL_FRect* src_rect
) {
    // 2D Frustum Culling Check
    if (m_active_camera && !m_active_camera->is_visible(position, size)) {
        m_culled_sprites++;
        return; // Early discard out-of-screen sprite
    }

    SpriteCommand cmd{texture, position, size, rotation, color};
    if (src_rect) {
        cmd.src_rect = *src_rect;
        cmd.has_src_rect = true;
    }
    m_sprite_queue.push_back(cmd);
}

void Renderer2D::flush_batch(Texture* texture) {
    if (m_vertex_buffer.empty() || !texture || !m_sdl_renderer) return;

    SDL_RenderGeometry(
        m_sdl_renderer,
        texture->get_sdl_texture(),
        m_vertex_buffer.data(),
        static_cast<int>(m_vertex_buffer.size()),
        m_index_buffer.data(),
        static_cast<int>(m_index_buffer.size())
    );

    m_draw_calls++;
    m_vertex_buffer.clear();
    m_index_buffer.clear();
}

void Renderer2D::end_scene() {
    if (!m_active_camera || !m_sdl_renderer) return;

    // Depth sorting (Z-depth first, then Y-sorting, then Texture grouping for batching)
    std::sort(m_sprite_queue.begin(), m_sprite_queue.end(), [](const SpriteCommand& a, const SpriteCommand& b) {
        if (a.position.z != b.position.z) {
            return a.position.z < b.position.z;
        }
        if (a.position.y != b.position.y) {
            return a.position.y < b.position.y;
        }
        return a.texture < b.texture;
    });

    Texture* current_texture = nullptr;

    for (const auto& cmd : m_sprite_queue) {
        if (!cmd.texture) continue;

        // Flush batch if texture switches
        if (cmd.texture != current_texture) {
            if (current_texture != nullptr) {
                flush_batch(current_texture);
            }
            current_texture = cmd.texture;
        }

        // Calculate GPU screen position based on camera parallax and Z-depth
        glm::vec2 screen_pos = m_active_camera->world_to_screen(cmd.position);
        float z_scale = m_active_camera->get_z_scale(cmd.position.z) * m_active_camera->get_zoom();

        glm::vec2 half_size = (cmd.size * z_scale) * 0.5f;

        // Corners relative to center
        glm::vec2 corners[4] = {
            {-half_size.x, -half_size.y},
            { half_size.x, -half_size.y},
            { half_size.x,  half_size.y},
            {-half_size.x,  half_size.y}
        };

        if (cmd.rotation != 0.0f) {
            float rad = glm::radians(cmd.rotation);
            float cos_a = std::cos(rad);
            float sin_a = std::sin(rad);

            for (int i = 0; i < 4; ++i) {
                float rx = corners[i].x * cos_a - corners[i].y * sin_a;
                float ry = corners[i].x * sin_a + corners[i].y * cos_a;
                corners[i] = glm::vec2(rx, ry);
            }
        }

        for (int i = 0; i < 4; ++i) {
            corners[i] += screen_pos;
        }

        // Normalize UVs (0.0f to 1.0f)
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
        if (cmd.has_src_rect && cmd.texture->get_width() > 0 && cmd.texture->get_height() > 0) {
            float tw = static_cast<float>(cmd.texture->get_width());
            float th = static_cast<float>(cmd.texture->get_height());
            u0 = cmd.src_rect.x / tw;
            v0 = cmd.src_rect.y / th;
            u1 = (cmd.src_rect.x + cmd.src_rect.w) / tw;
            v1 = (cmd.src_rect.y + cmd.src_rect.h) / th;
        }

        SDL_FColor col{
            cmd.color.r / 255.0f,
            cmd.color.g / 255.0f,
            cmd.color.b / 255.0f,
            cmd.color.a / 255.0f
        };

        int base_vertex = static_cast<int>(m_vertex_buffer.size());

        // Quad 4 vertices
        m_vertex_buffer.push_back(SDL_Vertex{{corners[0].x, corners[0].y}, col, {u0, v0}});
        m_vertex_buffer.push_back(SDL_Vertex{{corners[1].x, corners[1].y}, col, {u1, v0}});
        m_vertex_buffer.push_back(SDL_Vertex{{corners[2].x, corners[2].y}, col, {u1, v1}});
        m_vertex_buffer.push_back(SDL_Vertex{{corners[3].x, corners[3].y}, col, {u0, v1}});

        // Quad 6 indices (2 triangles)
        m_index_buffer.push_back(base_vertex + 0);
        m_index_buffer.push_back(base_vertex + 1);
        m_index_buffer.push_back(base_vertex + 2);

        m_index_buffer.push_back(base_vertex + 0);
        m_index_buffer.push_back(base_vertex + 2);
        m_index_buffer.push_back(base_vertex + 3);

        m_rendered_sprites++;
    }

    if (current_texture != nullptr) {
        flush_batch(current_texture);
    }
}

} // namespace Engine
