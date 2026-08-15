#include "CameraController2D.hpp"
#include <cmath>

namespace Engine {

CameraController2D::CameraController2D(Camera2D* camera)
    : m_camera(camera) {}

void CameraController2D::set_bounds(const glm::vec2& min_bounds, const glm::vec2& max_bounds) {
    m_min_bounds = glm::min(min_bounds, max_bounds);
    m_max_bounds = glm::max(min_bounds, max_bounds);
    m_use_bounds = true;
}

void CameraController2D::snap_to_target() {
    m_current_center = m_target_pos;
    m_lookahead_offset = glm::vec2(0.0f);
    if (m_camera) {
        m_camera->set_position(m_current_center);
    }
}

void CameraController2D::update(float dt) {
    if (!m_camera) return;

    // 1. Calculate desired target point considering Deadzone
    glm::vec2 desired_center = m_current_center;

    if (m_use_deadzone) {
        glm::vec2 delta = m_target_pos - m_current_center;

        if (std::abs(delta.x) > m_deadzone.x) {
            float sign_x = (delta.x > 0.0f) ? 1.0f : -1.0f;
            desired_center.x = m_target_pos.x - sign_x * m_deadzone.x;
        }
        if (std::abs(delta.y) > m_deadzone.y) {
            float sign_y = (delta.y > 0.0f) ? 1.0f : -1.0f;
            desired_center.y = m_target_pos.y - sign_y * m_deadzone.y;
        }
    } else {
        desired_center = m_target_pos;
    }

    // 2. Lookahead
    if (m_use_lookahead) {
        glm::vec2 target_lookahead(0.0f);
        float vel_len = glm::length(m_target_velocity);
        if (vel_len > 15.0f) {
            target_lookahead = (m_target_velocity / vel_len) * m_lookahead_distance;
        }
        m_lookahead_offset += (target_lookahead - m_lookahead_offset) * std::clamp(m_lookahead_speed * dt, 0.0f, 1.0f);
        desired_center += m_lookahead_offset;
    }

    // 3. Smooth Damped Follow (exponential decay lerp)
    float blend = 1.0f - std::exp(-m_follow_speed * dt);
    m_current_center += (desired_center - m_current_center) * blend;

    // 4. Level Bounds Clamping
    if (m_use_bounds) {
        float eff_zoom = m_camera->get_effective_zoom();
        if (eff_zoom > 0.001f) {
            glm::vec2 viewport = m_camera->get_viewport_size();
            float half_w = (viewport.x / eff_zoom) * 0.5f;
            float half_h = (viewport.y / eff_zoom) * 0.5f;

            // Clamp X
            if ((m_max_bounds.x - m_min_bounds.x) >= half_w * 2.0f) {
                m_current_center.x = std::clamp(m_current_center.x, m_min_bounds.x + half_w, m_max_bounds.x - half_w);
            } else {
                m_current_center.x = (m_min_bounds.x + m_max_bounds.x) * 0.5f;
            }

            // Clamp Y
            if ((m_max_bounds.y - m_min_bounds.y) >= half_h * 2.0f) {
                m_current_center.y = std::clamp(m_current_center.y, m_min_bounds.y + half_h, m_max_bounds.y - half_h);
            } else {
                m_current_center.y = (m_min_bounds.y + m_max_bounds.y) * 0.5f;
            }
        }
    }

    // 5. Trauma Screen Shake (Non-linear decay formula: shake = trauma^2)
    glm::vec2 shake_offset(0.0f);
    if (m_trauma > 0.001f) {
        m_shake_timer += dt;
        float shake_factor = m_trauma * m_trauma; // Non-linear response curve

        float sx = std::sin(m_shake_timer * m_shake_frequency);
        float sy = std::cos(m_shake_timer * m_shake_frequency * 1.37f);

        shake_offset.x = m_max_shake_offset * shake_factor * sx;
        shake_offset.y = m_max_shake_offset * shake_factor * sy;

        // Linear trauma decay
        m_trauma = std::max(0.0f, m_trauma - m_trauma_decay * dt);
    } else {
        m_trauma = 0.0f;
    }

    // 6. Push final combined position to Camera2D
    m_camera->set_position(m_current_center + shake_offset);
}

} // namespace Engine
