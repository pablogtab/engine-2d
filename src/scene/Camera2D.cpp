#include "Camera2D.hpp"
#include <algorithm>

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

} // namespace Engine
