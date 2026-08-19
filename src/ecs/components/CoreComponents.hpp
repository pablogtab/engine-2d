#pragma once

#include "../EcsTypes.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

struct TransformComponent {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec2 scale{1.0f, 1.0f};
    float rotation{0.0f};
    glm::vec2 pivot{0.5f, 0.5f};
    glm::vec2 skew{0.0f, 0.0f};
    bool visible{true};

    glm::vec3 world_position{0.0f, 0.0f, 0.0f};
    glm::vec2 world_scale{1.0f, 1.0f};
    float world_rotation{0.0f};
    glm::vec2 world_pivot{0.5f, 0.5f};
    glm::vec2 world_skew{0.0f, 0.0f};
    bool world_visible{true};

    TransformComponent() = default;
    TransformComponent(
        const glm::vec3& pos,
        const glm::vec2& scl = {1.0f, 1.0f},
        float rot = 0.0f,
        const glm::vec2& pvt = {0.5f, 0.5f}
    ) : position(pos),
        scale(scl),
        rotation(rot),
        pivot(pvt),
        world_position(pos),
        world_scale(scl),
        world_rotation(rot),
        world_pivot(pvt) {}

    TransformComponent(float x, float y, float z = 0.0f)
        : position(x, y, z),
          scale(1.0f, 1.0f),
          rotation(0.0f),
          pivot(0.5f, 0.5f),
          world_position(x, y, z),
          world_scale(1.0f, 1.0f),
          world_rotation(0.0f),
          world_pivot(0.5f, 0.5f) {}

    void look_at(const glm::vec2& target_world_pos) {
        const glm::vec2 direction = target_world_pos - glm::vec2(world_position.x, world_position.y);
        if (glm::length(direction) > 0.0001f) {
            rotation = glm::degrees(std::atan2(direction.y, direction.x));
        }
    }

    [[nodiscard]] glm::vec2 get_right() const {
        const float radians = glm::radians(world_rotation);
        return glm::vec2(std::cos(radians), std::sin(radians));
    }

    [[nodiscard]] glm::vec2 get_up() const {
        const float radians = glm::radians(world_rotation);
        return glm::vec2(-std::sin(radians), std::cos(radians));
    }
};

struct HierarchyComponent {
    EntityID parent{0};
    std::vector<EntityID> children;

    HierarchyComponent() = default;
    explicit HierarchyComponent(EntityID parent_id) : parent(parent_id) {}
};

struct TagComponent {
    std::string tag{"Entity"};

    TagComponent() = default;
    explicit TagComponent(const std::string& name) : tag(name) {}
};

struct BehaviorComponent {
    std::function<void(EntityID, float)> update_fn;

    BehaviorComponent() = default;
    explicit BehaviorComponent(std::function<void(EntityID, float)> fn) : update_fn(std::move(fn)) {}
};

} // namespace Engine
