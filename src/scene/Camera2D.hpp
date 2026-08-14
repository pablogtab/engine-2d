#pragma once

#include <glm/glm.hpp>

#include <SDL3/SDL.h>

namespace Engine {

class Camera2D {
public:
    Camera2D(float viewport_width, float viewport_height);

    void set_position(const glm::vec2& pos) { m_position = pos; }
    const glm::vec2& get_position() const { return m_position; }
    void move(const glm::vec2& offset) { m_position += offset; }

    void set_zoom(float zoom) { m_zoom = glm::clamp(zoom, 0.1f, 10.0f); }
    float get_zoom() const { return m_zoom; }

    void set_viewport_size(float width, float height) {
        m_viewport_size = glm::vec2(width, height);
    }
    const glm::vec2& get_viewport_size() const { return m_viewport_size; }

    // Converts a 2.5D world position (X, Y, Z) to GPU screen coordinates.
    // Z < 0: Background (moves slower with parallax, scales down)
    // Z = 0: Primary gameplay plane
    // Z > 0: Foreground (moves faster, scales up)
    glm::vec2 world_to_screen(const glm::vec3& world_pos) const;

    // Converts screen pixel coordinates back into 2.5D world coordinates
    glm::vec2 screen_to_world(const glm::vec2& screen_pos, float z = 0.0f) const;

    // Returns visual scale factor produced by Z distance
    float get_z_scale(float z) const;

    // Checks if a 2.5D sprite is within the visible camera viewport (Frustum Culling)
    bool is_visible(const glm::vec3& world_pos, const glm::vec2& size) const;

    // Returns the visible 2.5D world bounding box for spatial queries
    SDL_FRect get_world_bounds(float z = 0.0f) const;

private:
    glm::vec2 m_position{0.0f, 0.0f};
    glm::vec2 m_viewport_size{1280.0f, 720.0f};
    float m_zoom{1.0f};
};

} // namespace Engine
