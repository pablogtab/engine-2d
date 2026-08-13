#include "Camera2D.hpp"

namespace Engine {

Camera2D::Camera2D(float viewport_width, float viewport_height)
    : m_viewport_size(viewport_width, viewport_height) {}

float Camera2D::get_z_scale(float z) const {
    // Focal scaling formula for Z-depth:
    // Negative Z -> Distant object -> Scale < 1.0
    // Positive Z -> Foreground object -> Scale > 1.0
    if (z < 0.0f) {
        return 1.0f / (1.0f - z * 0.005f);
    } else {
        return 1.0f + z * 0.005f;
    }
}

glm::vec2 Camera2D::world_to_screen(const glm::vec3& world_pos) const {
    // Parallax factor determines displacement speed relative to camera
    float z_scale = get_z_scale(world_pos.z);
    
    // Parallax Factor: Distant layers (Z < 0) move slower
    float parallax_factor = z_scale;
    
    glm::vec2 relative_pos = (glm::vec2(world_pos.x, world_pos.y) - m_position * parallax_factor) * m_zoom * z_scale;
    glm::vec2 screen_center = m_viewport_size * 0.5f;

    return relative_pos + screen_center;
}

bool Camera2D::is_visible(const glm::vec3& world_pos, const glm::vec2& size) const {
    glm::vec2 screen_pos = world_to_screen(world_pos);
    float z_scale = get_z_scale(world_pos.z) * m_zoom;
    glm::vec2 half_size = (size * z_scale) * 0.5f;

    float min_x = screen_pos.x - half_size.x;
    float max_x = screen_pos.x + half_size.x;
    float min_y = screen_pos.y - half_size.y;
    float max_y = screen_pos.y + half_size.y;

    return !(max_x < 0.0f || min_x > m_viewport_size.x || 
             max_y < 0.0f || min_y > m_viewport_size.y);
}

SDL_FRect Camera2D::get_world_bounds(float z) const {
    float z_scale = get_z_scale(z) * m_zoom;
    glm::vec2 half_world_dim = (m_viewport_size / z_scale) * 0.5f;

    return SDL_FRect{
        m_position.x - half_world_dim.x,
        m_position.y - half_world_dim.y,
        half_world_dim.x * 2.0f,
        half_world_dim.y * 2.0f
    };
}

} // namespace Engine
