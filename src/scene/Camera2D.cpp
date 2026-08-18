#include "Camera2D.hpp"
#include <cmath>

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
    
    glm::vec2 delta = glm::vec2(
        world_pos.x - m_position.x * parallax_factor,
        world_pos.y - m_position.y * parallax_factor
    );
    const float radians = glm::radians(-m_rotation);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const glm::vec2 local(
        delta.x * cosine - delta.y * sine,
        delta.x * sine + delta.y * cosine
    );

    // Cartesian Y-up to screen Y-down mapping happens after camera rotation.
    glm::vec2 relative_pos(local.x, -local.y);
    relative_pos *= get_effective_zoom() * z_scale;
    glm::vec2 screen_center = m_viewport_size * 0.5f;

    return relative_pos + screen_center;
}

glm::vec2 Camera2D::screen_to_world(const glm::vec2& screen_pos, float z) const {
    glm::vec2 physical_screen_pos = screen_pos * m_dpi_scale;
    float z_scale = get_z_scale(z);
    float parallax_factor = z_scale;
    glm::vec2 screen_center = m_viewport_size * 0.5f;

    glm::vec2 relative_pos = physical_screen_pos - screen_center;
    const glm::vec2 local(
        relative_pos.x / (get_effective_zoom() * z_scale),
        -relative_pos.y / (get_effective_zoom() * z_scale)
    );
    const float radians = glm::radians(m_rotation);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const glm::vec2 delta(
        local.x * cosine - local.y * sine,
        local.x * sine + local.y * cosine
    );

    float world_x = delta.x + m_position.x * parallax_factor;
    float world_y = delta.y + m_position.y * parallax_factor;

    return glm::vec2(world_x, world_y);
}

bool Camera2D::is_visible(const glm::vec3& world_pos, const glm::vec2& size) const {
    glm::vec2 screen_pos = world_to_screen(world_pos);
    float z_scale = get_z_scale(world_pos.z) * get_effective_zoom();
    glm::vec2 half_size = (size * z_scale) * 0.5f;

    float min_x = screen_pos.x - half_size.x;
    float max_x = screen_pos.x + half_size.x;
    float min_y = screen_pos.y - half_size.y;
    float max_y = screen_pos.y + half_size.y;

    return !(max_x < 0.0f || min_x > m_viewport_size.x || 
             max_y < 0.0f || min_y > m_viewport_size.y);
}

SDL_FRect Camera2D::get_world_bounds(float z) const {
    float z_scale = get_z_scale(z) * get_effective_zoom();
    glm::vec2 half_world_dim = (m_viewport_size / z_scale) * 0.5f;
    const float radians = glm::radians(m_rotation);
    const float cosine = std::abs(std::cos(radians));
    const float sine = std::abs(std::sin(radians));
    const glm::vec2 rotated_extent(
        half_world_dim.x * cosine + half_world_dim.y * sine,
        half_world_dim.x * sine + half_world_dim.y * cosine
    );

    return SDL_FRect{
        m_position.x - rotated_extent.x,
        m_position.y - rotated_extent.y,
        rotated_extent.x * 2.0f,
        rotated_extent.y * 2.0f
    };
}

} // namespace Engine
