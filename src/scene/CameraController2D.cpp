#include "CameraController2D.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../ecs/EntityManager.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/RenderingComponents.hpp"
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

void CameraController2D::reset_center(const glm::vec2& center) {
    m_current_center = center;
    m_target_pos = center;
    m_target_velocity = glm::vec2(0.0f);
    m_lookahead_offset = glm::vec2(0.0f);
    if (m_camera) m_camera->set_position(center);
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
        const glm::vec2 half_extent = m_camera->get_world_half_extents();
        if (half_extent.x > 0.001f && half_extent.y > 0.001f) {

            // Clamp X
            if ((m_max_bounds.x - m_min_bounds.x) >= half_extent.x * 2.0f) {
                m_current_center.x = std::clamp(m_current_center.x, m_min_bounds.x + half_extent.x, m_max_bounds.x - half_extent.x);
            } else {
                m_current_center.x = (m_min_bounds.x + m_max_bounds.x) * 0.5f;
            }

            // Clamp Y
            if ((m_max_bounds.y - m_min_bounds.y) >= half_extent.y * 2.0f) {
                m_current_center.y = std::clamp(m_current_center.y, m_min_bounds.y + half_extent.y, m_max_bounds.y - half_extent.y);
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

bool CameraController2D::update_from_entities(
    EntityManager& entity_manager,
    float dt,
    bool reset_tracking
) {
    if (!m_camera) return false;

    const EntityID primary = CameraSystem::find_primary(entity_manager);
    if (primary == 0) {
        m_active_camera_entity = 0;
        reset_center(glm::vec2(0.0f));
        m_camera->set_rotation(0.0f);
        m_camera->set_zoom(1.0f);
        return false;
    }

    const bool camera_changed = primary != m_active_camera_entity;
    m_active_camera_entity = primary;
    if (!CameraSystem::apply_to_runtime(entity_manager, primary, *m_camera, true)) return false;

    const auto& camera = entity_manager.get_component<CameraComponent>(primary);
    const auto& transform = entity_manager.get_component<TransformComponent>(primary);
    const glm::vec2 authored_position(transform.world_position);
    if (reset_tracking || camera_changed) reset_center(authored_position);

    set_follow_speed(camera.follow_speed);
    enable_bounds(camera.enable_bounds);
    if (camera.enable_bounds) {
        set_bounds(
            glm::vec2(camera.bounds.x, camera.bounds.y),
            glm::vec2(camera.bounds.z, camera.bounds.w)
        );
    }

    if (camera.follow_target == 0 ||
        !entity_manager.is_alive(camera.follow_target) ||
        !entity_manager.has_component<TransformComponent>(camera.follow_target)) {
        reset_center(authored_position);
        return true;
    }

    const auto& target = entity_manager.get_component<TransformComponent>(camera.follow_target);
    set_target(glm::vec2(target.world_position) + camera.follow_offset);
    update(dt);
    return true;
}

} // namespace Engine
