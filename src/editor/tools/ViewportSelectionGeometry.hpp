#pragma once

#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../ecs/EntityManager.hpp"
#include "../../tilemap/Tilemap.hpp"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Engine {

// Shared editor-space geometry. Keeping every selection operation on this
// representation prevents outlines, picking and marquee selection drifting apart.
struct ViewportSelectionQuad {
    std::array<glm::vec2, 4> corners{};
};

inline glm::vec2 rotate_editor_world_point(const glm::vec2& point, float rotation_degrees) {
    const float radians = glm::radians(-rotation_degrees);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    return glm::vec2(point.x * cosine - point.y * sine, point.x * sine + point.y * cosine);
}

inline ViewportSelectionQuad make_selection_quad(
    const TransformComponent& transform,
    const glm::vec2& local_min,
    const glm::vec2& local_max
) {
    ViewportSelectionQuad quad;
    const glm::vec2 origin(transform.world_position.x, transform.world_position.y);
    const std::array<glm::vec2, 4> local_corners{{
        {local_min.x, local_max.y},
        {local_max.x, local_max.y},
        {local_max.x, local_min.y},
        {local_min.x, local_min.y}
    }};

    for (size_t index = 0; index < local_corners.size(); ++index) {
        quad.corners[index] = origin + rotate_editor_world_point(local_corners[index], transform.world_rotation);
    }
    return quad;
}

inline bool get_selection_quad(EntityManager& entity_manager, EntityID id, ViewportSelectionQuad& quad) {
    if (!entity_manager.is_alive(id) || !entity_manager.has_component<TransformComponent>(id)) return false;
    const auto& transform = entity_manager.get_component<TransformComponent>(id);

    if (entity_manager.has_component<SpriteComponent>(id)) {
        const auto& sprite = entity_manager.get_component<SpriteComponent>(id);
        const glm::vec2 size = sprite.size * transform.world_scale;
        quad = make_selection_quad(
            transform,
            glm::vec2(-transform.world_pivot.x * size.x, -(1.0f - transform.world_pivot.y) * size.y),
            glm::vec2((1.0f - transform.world_pivot.x) * size.x, transform.world_pivot.y * size.y)
        );
        return true;
    }

    if (entity_manager.has_component<TilemapComponent>(id)) {
        const auto& tilemap = entity_manager.get_component<TilemapComponent>(id);
        int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
        if (!tilemap.empty()) tilemap.get_bounds(min_x, min_y, max_x, max_y);
        const float width = static_cast<float>(max_x - min_x + 1) * tilemap.tile_size * transform.world_scale.x;
        const float height = static_cast<float>(max_y - min_y + 1) * tilemap.tile_size * transform.world_scale.y;
        quad = make_selection_quad(
            transform,
            glm::vec2(static_cast<float>(min_x) * tilemap.tile_size * transform.world_scale.x, -static_cast<float>(max_y + 1) * tilemap.tile_size * transform.world_scale.y),
            glm::vec2(static_cast<float>(min_x) * tilemap.tile_size * transform.world_scale.x + width, -static_cast<float>(max_y + 1) * tilemap.tile_size * transform.world_scale.y + height)
        );
        return true;
    }

    if (!entity_manager.has_component<ColliderComponent>(id)) return false;
    const auto& collider = entity_manager.get_component<ColliderComponent>(id);
    if (collider.shapes.empty()) return false;

    glm::vec2 local_min(std::numeric_limits<float>::max());
    glm::vec2 local_max(std::numeric_limits<float>::lowest());
    for (const auto& shape : collider.shapes) {
        glm::vec2 half = shape.size * transform.world_scale * 0.5f;
        if (shape.type == ColliderShapeType::Circle) half = glm::vec2(shape.radius) * transform.world_scale;
        const glm::vec2 offset = shape.offset * transform.world_scale;
        local_min = glm::min(local_min, offset - half);
        local_max = glm::max(local_max, offset + half);
        for (const auto& vertex : shape.vertices) {
            const glm::vec2 point = offset + vertex * transform.world_scale;
            local_min = glm::min(local_min, point);
            local_max = glm::max(local_max, point);
        }
    }
    quad = make_selection_quad(transform, local_min, local_max);
    return true;
}

inline bool point_in_selection_quad(const glm::vec2& point, const ViewportSelectionQuad& quad) {
    bool has_positive = false;
    bool has_negative = false;
    for (size_t index = 0; index < quad.corners.size(); ++index) {
        const glm::vec2& a = quad.corners[index];
        const glm::vec2& b = quad.corners[(index + 1) % quad.corners.size()];
        const glm::vec2 edge = b - a;
        const glm::vec2 to_point = point - a;
        const float cross = edge.x * to_point.y - edge.y * to_point.x;
        has_positive |= cross > 0.0001f;
        has_negative |= cross < -0.0001f;
        if (has_positive && has_negative) return false;
    }
    return true;
}

inline bool point_in_world_rect(const glm::vec2& point, const glm::vec2& min, const glm::vec2& max) {
    return point.x >= min.x && point.x <= max.x && point.y >= min.y && point.y <= max.y;
}

inline float orientation(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

inline bool segments_intersect(const glm::vec2& a, const glm::vec2& b, const glm::vec2& c, const glm::vec2& d) {
    const float ab_c = orientation(a, b, c);
    const float ab_d = orientation(a, b, d);
    const float cd_a = orientation(c, d, a);
    const float cd_b = orientation(c, d, b);
    return ((ab_c > 0.0f && ab_d < 0.0f) || (ab_c < 0.0f && ab_d > 0.0f)) &&
           ((cd_a > 0.0f && cd_b < 0.0f) || (cd_a < 0.0f && cd_b > 0.0f));
}

inline bool selection_quad_intersects_world_rect(const ViewportSelectionQuad& quad, const glm::vec2& min, const glm::vec2& max) {
    for (const auto& corner : quad.corners) if (point_in_world_rect(corner, min, max)) return true;
    const std::array<glm::vec2, 4> rect{{{min.x, min.y}, {max.x, min.y}, {max.x, max.y}, {min.x, max.y}}};
    for (const auto& corner : rect) if (point_in_selection_quad(corner, quad)) return true;
    for (size_t a = 0; a < 4; ++a) {
        for (size_t b = 0; b < 4; ++b) {
            if (segments_intersect(quad.corners[a], quad.corners[(a + 1) % 4], rect[b], rect[(b + 1) % 4])) return true;
        }
    }
    return false;
}

} // namespace Engine
