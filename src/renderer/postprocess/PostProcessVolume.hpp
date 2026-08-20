#pragma once

#include "PostProcessProfile.hpp"
#include "../../core/AssetDatabase.hpp"
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <cmath>
#include <optional>

namespace Engine {

enum class VolumeShape {
    Box,
    Circle
};

// ---------------------------------------------------------------------------
// PostProcessVolume
// ---------------------------------------------------------------------------
// Volumen espacial 2D que define una atmósfera o perfil visual en el mundo.
// Calcula un peso de mezcla (weight 0.0f -> 1.0f) con transición suave (feathering)
// basado en la posición del jugador o de la cámara.
// ---------------------------------------------------------------------------
class PostProcessVolume {
public:
    PostProcessVolume(
        const std::string& name,
        const glm::vec2& position,
        const glm::vec2& size,
        AssetHandle profile_handle,
        float blend_distance = 64.0f,
        int priority = 0
    ) : m_name(name),
        m_position(position),
        m_size(size),
        m_profile_handle(profile_handle),
        m_blend_distance(std::max(0.1f, blend_distance)),
        m_priority(priority),
        m_shape(VolumeShape::Box) {}

    static PostProcessVolume Circle(
        const std::string& name,
        const glm::vec2& center,
        float radius,
        AssetHandle profile_handle,
        float blend_distance = 64.0f,
        int priority = 0
    ) {
        PostProcessVolume v(name, center, glm::vec2(radius * 2.0f), profile_handle, blend_distance, priority);
        v.m_shape = VolumeShape::Circle;
        v.m_radius = radius;
        return v;
    }

    float calculate_weight(const glm::vec2& point) const {
        if (!m_enabled) return 0.0f;

        if (m_shape == VolumeShape::Circle) {
            float dist = glm::distance(point, m_position);
            if (dist <= m_radius) {
                return 1.0f;
            } else if (dist <= m_radius + m_blend_distance) {
                float t = (dist - m_radius) / m_blend_distance;
                return 1.0f - std::clamp(t, 0.0f, 1.0f);
            }
            return 0.0f;
        }

        // Box volume
        glm::vec2 half_size = m_size * 0.5f;
        glm::vec2 min_pt = m_position - half_size;
        glm::vec2 max_pt = m_position + half_size;

        if (point.x >= min_pt.x && point.x <= max_pt.x &&
            point.y >= min_pt.y && point.y <= max_pt.y) {
            return 1.0f;
        }

        // Calculate distance outside box for smooth feathering
        float dx = std::max({min_pt.x - point.x, 0.0f, point.x - max_pt.x});
        float dy = std::max({min_pt.y - point.y, 0.0f, point.y - max_pt.y});
        float dist = std::sqrt(dx * dx + dy * dy);

        if (dist <= m_blend_distance) {
            float t = dist / m_blend_distance;
            return 1.0f - std::clamp(t, 0.0f, 1.0f);
        }

        return 0.0f;
    }

    const std::string& get_name() const { return m_name; }
    [[nodiscard]] AssetHandle profile_handle() const { return m_profile_handle; }
    void set_profile_handle(AssetHandle handle) { m_profile_handle = handle; }
    [[nodiscard]] std::optional<PostProcessProfile> load_profile(const AssetDatabase& database) const {
        return PostProcessProfile::load_asset(m_profile_handle, database);
    }

    const glm::vec2& get_position() const { return m_position; }
    void set_position(const glm::vec2& pos) { m_position = pos; }

    const glm::vec2& get_size() const { return m_size; }
    void set_size(const glm::vec2& size) { m_size = size; }

    int get_priority() const { return m_priority; }
    void set_priority(int p) { m_priority = p; }

    bool is_enabled() const { return m_enabled; }
    void set_enabled(bool e) { m_enabled = e; }

private:
    std::string m_name{"Volume"};
    glm::vec2 m_position{0.0f, 0.0f};
    glm::vec2 m_size{200.0f, 200.0f};
    float m_radius{100.0f};
    AssetHandle m_profile_handle{};
    float m_blend_distance{64.0f};
    int m_priority{0};
    bool m_enabled{true};
    VolumeShape m_shape{VolumeShape::Box};
};

// ---------------------------------------------------------------------------
// PostProcessVolumeManager
// ---------------------------------------------------------------------------
class PostProcessVolumeManager {
public:
    void add_volume(const PostProcessVolume& volume) {
        m_volumes.push_back(volume);
    }

    void clear() {
        m_volumes.clear();
    }

    PostProcessProfile evaluate(
        const glm::vec2& point,
        const PostProcessProfile& base_profile,
        const AssetDatabase& database
    ) const {
        if (m_volumes.empty()) {
            return base_profile;
        }

        PostProcessProfile blended = base_profile;
        for (const auto& vol : m_volumes) {
            float weight = vol.calculate_weight(point);
            if (weight > 0.001f) {
                if (const auto profile = vol.load_profile(database)) {
                    blended = PostProcessProfile::lerp(blended, *profile, weight);
                }
            }
        }
        return blended;
    }

    const std::vector<PostProcessVolume>& get_volumes() const { return m_volumes; }
    std::vector<PostProcessVolume>& get_volumes() { return m_volumes; }

private:
    std::vector<PostProcessVolume> m_volumes;
};

} // namespace Engine
