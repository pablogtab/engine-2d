#pragma once

#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

// Box2D supports up to eight polygon vertices. Reject invalid authored data
// before fixture construction, including geometry that welding would collapse.
inline constexpr std::size_t collider_max_polygon_vertices = 8;
inline bool collider_shape_valid(const ColliderShape& shape, glm::vec2 scale = {1.0f, 1.0f}) {
    const auto finite = [](glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); };
    if (!finite(scale) || !finite(shape.offset * scale)) return false;
    if (shape.type == ColliderShapeType::Box) {
        const auto size = glm::abs(shape.size * scale);
        return finite(shape.size) && shape.size.x > 0 && shape.size.y > 0 &&
            finite(size) && size.x > 0 && size.y > 0;
    }
    if (shape.type == ColliderShapeType::Circle) {
        const float radius = std::abs(shape.radius * scale.x);
        return std::isfinite(shape.radius) && shape.radius > 0 && std::isfinite(radius) && radius > 0;
    }
    if (shape.type != ColliderShapeType::Polygon || shape.vertices.size() < 3 ||
        shape.vertices.size() > collider_max_polygon_vertices) return false;
    // Box2D welds points closer than half its linear slop (0.25 engine pixels).
    bool non_collinear = false;
    for (std::size_t i = 0; i < shape.vertices.size(); ++i) {
        const auto a = shape.vertices[i] * scale;
        if (!finite(a) || !finite(a + shape.offset * scale)) return false;
        for (std::size_t j = 0; j < i; ++j) {
            const auto b = shape.vertices[j] * scale;
            if (glm::dot(a - b, a - b) < 0.25f * 0.25f) return false;
            for (std::size_t k = 0; k < j; ++k) {
                const auto c = shape.vertices[k] * scale;
                const double cross = double(a.x - c.x) * double(b.y - c.y) -
                    double(a.y - c.y) * double(b.x - c.x);
                if (std::abs(cross) > 0.01) non_collinear = true;
            }
        }
    }
    return non_collinear;
}

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
