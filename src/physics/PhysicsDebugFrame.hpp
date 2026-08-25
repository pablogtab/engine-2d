#pragma once

#include "../ecs/EntityManager.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Engine {

enum class PhysicsDebugShapeType { Circle, Polygon };

struct PhysicsDebugCollider {
    EntityID entity{0};
    PhysicsDebugShapeType type{PhysicsDebugShapeType::Polygon};
    std::vector<glm::vec2> points;
    float radius{0.0f};
    bool trigger{false};
    bool one_way{false};
    std::uint16_t category{0};
    std::uint16_t mask{0};
};

struct PhysicsDebugBody {
    EntityID entity{0};
    int body_type{0};
    glm::vec2 position{0.0f};
    glm::vec2 center_of_mass{0.0f};
    glm::vec2 velocity{0.0f};
    float angular_velocity{0.0f};
};

struct PhysicsDebugContact {
    EntityID entity_a{0};
    EntityID entity_b{0};
    glm::vec2 point{0.0f};
    glm::vec2 normal{0.0f};
    bool trigger{false};
};

struct PhysicsDebugFrame {
    std::uint64_t revision{0};
    std::vector<PhysicsDebugCollider> colliders;
    std::vector<PhysicsDebugBody> bodies;
    std::vector<PhysicsDebugContact> contacts;
};

struct PhysicsDebugOptions {
    bool colliders{false};
    bool contacts{false};
    bool normals{false};
    bool velocities{false};
    bool centers_of_mass{false};
    bool layers{false};
    bool selection_only{false};
    std::uint16_t category_filter{0};
    std::uint16_t mask_filter{0};
    [[nodiscard]] bool enabled() const {
        return colliders || contacts || normals || velocities || centers_of_mass || layers;
    }
};

} // namespace Engine
