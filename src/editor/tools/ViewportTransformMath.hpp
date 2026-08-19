#pragma once

#include "../../ecs/components/CoreComponents.hpp"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Engine {

struct ViewportTransformMath {
    // Computes the geometric centroid of a collection of world positions
    [[nodiscard]] static glm::vec3 compute_centroid(const std::vector<glm::vec3>& positions) {
        if (positions.empty()) return glm::vec3(0.0f);
        glm::vec3 sum(0.0f);
        for (const auto& pos : positions) sum += pos;
        return sum / static_cast<float>(positions.size());
    }

    // Calculates target world transform for translation
    [[nodiscard]] static TransformComponent calculate_move_target(
        const TransformComponent& start_world,
        const glm::vec2& constrained_world_delta
    ) {
        TransformComponent target = start_world;
        target.world_position = start_world.world_position + glm::vec3(constrained_world_delta, 0.0f);
        return target;
    }

    // Calculates target world transform for rotation (orbiting around centroid + orienting)
    [[nodiscard]] static TransformComponent calculate_rotate_target(
        const TransformComponent& start_world,
        const glm::vec3& centroid,
        float angle_delta_degrees
    ) {
        TransformComponent target = start_world;
        const float rad = glm::radians(angle_delta_degrees);
        const float cos_a = std::cos(rad);
        const float sin_a = std::sin(rad);

        const glm::vec2 rel_pos = glm::vec2(
            start_world.world_position.x - centroid.x,
            start_world.world_position.y - centroid.y
        );
        const glm::vec2 rotated_rel = glm::vec2(
            rel_pos.x * cos_a - rel_pos.y * sin_a,
            rel_pos.x * sin_a + rel_pos.y * cos_a
        );
        target.world_position = glm::vec3(
            centroid.x + rotated_rel.x,
            centroid.y + rotated_rel.y,
            start_world.world_position.z
        );
        target.world_rotation = start_world.world_rotation + angle_delta_degrees;
        return target;
    }

    // Calculates target world transform for scaling (dilating distance from centroid + sign-preserving rescale)
    [[nodiscard]] static TransformComponent calculate_scale_target(
        const TransformComponent& start_world,
        const glm::vec3& centroid,
        const glm::vec2& scale_vec
    ) {
        TransformComponent target = start_world;
        const glm::vec2 rel_pos = glm::vec2(
            start_world.world_position.x - centroid.x,
            start_world.world_position.y - centroid.y
        );
        const glm::vec2 scaled_rel = rel_pos * scale_vec;
        target.world_position = glm::vec3(
            centroid.x + scaled_rel.x,
            centroid.y + scaled_rel.y,
            start_world.world_position.z
        );

        const float sgn_x = (start_world.world_scale.x < 0.0f) ? -1.0f : 1.0f;
        const float sgn_y = (start_world.world_scale.y < 0.0f) ? -1.0f : 1.0f;
        target.world_scale.x = sgn_x * std::max(std::abs(start_world.world_scale.x) * scale_vec.x, 0.01f);
        target.world_scale.y = sgn_y * std::max(std::abs(start_world.world_scale.y) * scale_vec.y, 0.01f);
        return target;
    }
};

} // namespace Engine
