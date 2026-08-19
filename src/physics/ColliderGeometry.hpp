#pragma once

#include "../ecs/Components.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

// Shared collider geometry policy for Box2D and editor visualization. Keeping
// scaling and offset rules here prevents the debug outline from disagreeing
// with the fixture that actually participates in collisions.
inline glm::vec2 collider_scaled_offset(
    const ColliderShape& shape,
    const TransformComponent& transform
) {
    return shape.offset * transform.world_scale;
}

inline glm::vec2 collider_scaled_box_half_extent(
    const ColliderShape& shape,
    const TransformComponent& transform
) {
    return glm::abs(shape.size * transform.world_scale) * 0.5f;
}

inline float collider_scaled_circle_radius(
    const ColliderShape& shape,
    const TransformComponent& transform
) {
    // Box2D circles cannot represent non-uniform scaling. Match the existing
    // engine policy by using the local X axis, while keeping the radius valid
    // for mirrored transforms.
    return std::abs(shape.radius * transform.world_scale.x);
}

inline std::vector<glm::vec2> collider_scaled_polygon_vertices(
    const ColliderShape& shape,
    const TransformComponent& transform
) {
    std::vector<glm::vec2> result;
    result.reserve(shape.vertices.size());
    const glm::vec2 offset = collider_scaled_offset(shape, transform);
    for (const glm::vec2& vertex : shape.vertices) {
        result.push_back(offset + vertex * transform.world_scale);
    }
    return result;
}

inline glm::vec2 collider_local_to_world(
    const glm::vec2& local,
    const TransformComponent& transform
) {
    const float radians = glm::radians(-transform.world_rotation);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    const glm::vec2 rotated(
        local.x * cosine - local.y * sine,
        local.x * sine + local.y * cosine
    );
    return glm::vec2(transform.world_position) + rotated;
}

} // namespace Engine
