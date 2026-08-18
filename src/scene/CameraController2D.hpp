#pragma once

#include "Camera2D.hpp"
#include <glm/glm.hpp>
#include <algorithm>

namespace Engine {

// ---------------------------------------------------------------------------
// CameraController2D
// Optional, decoupled 2D camera director that drives an Engine::Camera2D.
// Provides smooth follow, deadzone, lookahead, level bounding box clamping,
// and trauma-based non-linear screen shake.
// ---------------------------------------------------------------------------
class CameraController2D {
public:
    explicit CameraController2D(Camera2D* camera = nullptr);

    void bind_camera(Camera2D* camera) { m_camera = camera; }
    [[nodiscard]] Camera2D* get_camera() const { return m_camera; }

    // --- Target Tracking ---
    void set_target(const glm::vec2& target_pos) { m_target_pos = target_pos; }
    [[nodiscard]] const glm::vec2& get_target() const { return m_target_pos; }

    void set_target_velocity(const glm::vec2& velocity) { m_target_velocity = velocity; }

    // Smooth follow speed multiplier (higher = snappier, lower = smoother)
    void set_follow_speed(float speed) { m_follow_speed = std::max(0.1f, speed); }
    [[nodiscard]] float get_follow_speed() const { return m_follow_speed; }

    // Instantly snaps the camera to the target position (useful on respawns / level load)
    void snap_to_target();
    void reset_center(const glm::vec2& center);

    // --- Deadzone (tolerance area in world units where target moves without moving camera) ---
    void set_deadzone(const glm::vec2& half_extents) { m_deadzone = glm::max(glm::vec2(0.0f), half_extents); }
    void enable_deadzone(bool enable) { m_use_deadzone = enable; }
    [[nodiscard]] bool is_deadzone_enabled() const { return m_use_deadzone; }
    [[nodiscard]] const glm::vec2& get_deadzone() const { return m_deadzone; }

    // --- Lookahead (leads the camera in the direction of velocity) ---
    void set_lookahead(float distance, float speed = 3.0f) {
        m_lookahead_distance = std::max(0.0f, distance);
        m_lookahead_speed = std::max(0.1f, speed);
    }
    void enable_lookahead(bool enable) { m_use_lookahead = enable; }

    // --- Level Boundaries (clamps camera so viewport never renders void space) ---
    void set_bounds(const glm::vec2& min_bounds, const glm::vec2& max_bounds);
    void enable_bounds(bool enable) { m_use_bounds = enable; }
    [[nodiscard]] bool are_bounds_enabled() const { return m_use_bounds; }

    // --- Trauma Screen Shake ---
    // Adds trauma [0.0, 1.0]. Trauma decays over time and produces shake proportional to trauma^2.
    void add_trauma(float amount) {
        m_trauma = std::clamp(m_trauma + amount, 0.0f, 1.0f);
    }
    void set_trauma(float amount) {
        m_trauma = std::clamp(amount, 0.0f, 1.0f);
    }
    [[nodiscard]] float get_trauma() const { return m_trauma; }

    void set_shake_parameters(float max_offset_px, float decay_per_second = 1.6f, float frequency = 28.0f) {
        m_max_shake_offset = max_offset_px;
        m_trauma_decay = decay_per_second;
        m_shake_frequency = frequency;
    }

    // --- Per-frame update ---
    void update(float dt);

private:
    Camera2D* m_camera{nullptr};

    // Tracking
    glm::vec2 m_target_pos{0.0f, 0.0f};
    glm::vec2 m_target_velocity{0.0f, 0.0f};
    glm::vec2 m_current_center{0.0f, 0.0f};
    glm::vec2 m_lookahead_offset{0.0f, 0.0f};
    float m_follow_speed{5.0f};

    // Deadzone
    glm::vec2 m_deadzone{0.0f, 0.0f};
    bool m_use_deadzone{false};

    // Lookahead
    float m_lookahead_distance{0.0f};
    float m_lookahead_speed{3.0f};
    bool m_use_lookahead{false};

    // Bounds
    glm::vec2 m_min_bounds{-1000.0f, -1000.0f};
    glm::vec2 m_max_bounds{1000.0f, 1000.0f};
    bool m_use_bounds{false};

    // Screen Shake
    float m_trauma{0.0f};
    float m_trauma_decay{1.6f};
    float m_max_shake_offset{24.0f};
    float m_shake_frequency{28.0f};
    float m_shake_timer{0.0f};
};

} // namespace Engine
