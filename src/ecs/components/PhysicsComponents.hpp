#pragma once

#include "../EcsTypes.hpp"
#include "../../physics/BezierCurve.hpp"
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>

namespace Engine {

namespace CollisionCategory {
inline constexpr std::uint16_t NONE = 0x0000;
inline constexpr std::uint16_t DEFAULT = 0x0001;
inline constexpr std::uint16_t PLAYER = 0x0002;
inline constexpr std::uint16_t GROUND = 0x0004;
inline constexpr std::uint16_t COIN = 0x0008;
inline constexpr std::uint16_t ENEMY = 0x0010;
inline constexpr std::uint16_t ALL = 0xFFFF;
}

enum class ColliderShapeType { Box, Circle, Polygon };

struct ColliderShape {
    ColliderShapeType type{ColliderShapeType::Box};
    glm::vec2 offset{0.0f, 0.0f};
    glm::vec2 size{32.0f, 32.0f};
    float radius{16.0f};
    std::vector<glm::vec2> vertices;
    bool is_trigger{false};
    bool is_one_way{false};
    std::uint16_t category_bits{CollisionCategory::DEFAULT};
    std::uint16_t mask_bits{CollisionCategory::ALL};
};

struct ColliderComponent {
    std::vector<ColliderShape> shapes;

    ColliderComponent() = default;

    static ColliderComponent Box(
        const glm::vec2& size,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        ColliderComponent component;
        component.add_box(size, offset, is_trigger, category, mask);
        return component;
    }

    static ColliderComponent OneWayBox(
        const glm::vec2& size,
        const glm::vec2& offset = {0.0f, 0.0f},
        std::uint16_t category = CollisionCategory::GROUND,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        ColliderComponent component;
        component.add_one_way_box(size, offset, category, mask);
        return component;
    }

    static ColliderComponent Circle(
        float radius,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        ColliderComponent component;
        component.add_circle(radius, offset, is_trigger, category, mask);
        return component;
    }

    static ColliderComponent Polygon(
        const std::vector<glm::vec2>& vertices,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        ColliderComponent component;
        component.add_polygon(vertices, offset, is_trigger, category, mask);
        return component;
    }

    static ColliderComponent Slope(
        float width,
        float height,
        bool slope_up_right = true,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        ColliderComponent component;
        component.add_slope(width, height, slope_up_right, offset, is_trigger, category, mask);
        return component;
    }

    ColliderComponent& add_box(
        const glm::vec2& size,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        shapes.push_back({ColliderShapeType::Box, offset, size, 0.0f, {}, is_trigger, false, category, mask});
        return *this;
    }

    ColliderComponent& add_one_way_box(
        const glm::vec2& size,
        const glm::vec2& offset = {0.0f, 0.0f},
        std::uint16_t category = CollisionCategory::GROUND,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        shapes.push_back({ColliderShapeType::Box, offset, size, 0.0f, {}, false, true, category, mask});
        return *this;
    }

    ColliderComponent& add_circle(
        float radius,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        shapes.push_back({ColliderShapeType::Circle, offset, {0.0f, 0.0f}, radius, {}, is_trigger, false, category, mask});
        return *this;
    }

    ColliderComponent& add_polygon(
        const std::vector<glm::vec2>& vertices,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        shapes.push_back({ColliderShapeType::Polygon, offset, {0.0f, 0.0f}, 0.0f, vertices, is_trigger, false, category, mask});
        return *this;
    }

    ColliderComponent& add_slope(
        float width,
        float height,
        bool slope_up_right = true,
        const glm::vec2& offset = {0.0f, 0.0f},
        bool is_trigger = false,
        std::uint16_t category = CollisionCategory::DEFAULT,
        std::uint16_t mask = CollisionCategory::ALL
    ) {
        const float half_width = width * 0.5f;
        const float half_height = height * 0.5f;
        std::vector<glm::vec2> vertices;
        if (slope_up_right) {
            vertices = {
                {-half_width, -half_height},
                {half_width, -half_height},
                {half_width, half_height}
            };
        } else {
            vertices = {
                {-half_width, -half_height},
                {half_width, -half_height},
                {-half_width, half_height}
            };
        }
        return add_polygon(vertices, offset, is_trigger, category, mask);
    }
};

enum class ForceMode {
    Impulse,
    ContinuousForce,
    VelocityChange,
    CurveOverTime
};

struct ActiveForce {
    glm::vec2 direction{0.0f, 1.0f};
    float max_magnitude{1000.0f};
    float duration{1.0f};
    float elapsed_time{0.0f};
    ForceMode mode{ForceMode::CurveOverTime};
    BezierCurve curve;
    bool finished{false};
};

enum class BodyType {
    Dynamic,
    Static,
    Kinematic
};

struct RigidBodyComponent {
    BodyType body_type{BodyType::Dynamic};
    glm::vec2 velocity{0.0f, 0.0f};
    glm::vec2 acceleration{0.0f, 0.0f};
    float angular_velocity{0.0f};
    float mass{1.0f};
    float gravity_scale{1.0f};
    float friction{0.4f};
    bool fixed_rotation{true};
    float restitution{0.2f};
    std::vector<ActiveForce> active_forces;

    RigidBodyComponent() = default;
    explicit RigidBodyComponent(
        BodyType type,
        float gravity = 1.0f,
        float bounciness = 0.2f,
        float surface_friction = 0.4f,
        bool lock_rotation = true
    ) : body_type(type),
        fixed_rotation(lock_rotation),
        gravity_scale((type == BodyType::Static || type == BodyType::Kinematic) ? 0.0f : gravity),
        restitution(bounciness),
        friction(surface_friction) {}

    static RigidBodyComponent Static(float surface_friction = 0.4f, float bounciness = 0.0f) {
        return RigidBodyComponent(BodyType::Static, 0.0f, bounciness, surface_friction, true);
    }

    static RigidBodyComponent Dynamic(
        float gravity = 1.0f,
        float bounciness = 0.2f,
        float surface_friction = 0.4f,
        bool lock_rotation = true
    ) {
        return RigidBodyComponent(BodyType::Dynamic, gravity, bounciness, surface_friction, lock_rotation);
    }

    static RigidBodyComponent Kinematic(float surface_friction = 0.8f) {
        return RigidBodyComponent(BodyType::Kinematic, 0.0f, 0.0f, surface_friction, true);
    }
};

enum class JointType {
    Distance,
    Rope,
    Revolute,
    Prismatic
};

struct DistanceJointConfig {
    float length{-1.0f};
    float min_length{0.0f};
    float max_length{-1.0f};
    float stiffness{0.0f};
    float damping{0.0f};
};

struct RevoluteJointConfig {
    bool enable_limit{false};
    float lower_angle{0.0f};
    float upper_angle{0.0f};
    bool enable_motor{false};
    float motor_speed{0.0f};
    float max_motor_torque{0.0f};
};

struct PrismaticJointConfig {
    glm::vec2 axis{1.0f, 0.0f};
    bool enable_limit{false};
    float lower_translation{0.0f};
    float upper_translation{0.0f};
    bool enable_motor{false};
    float motor_speed{0.0f};
    float max_motor_force{0.0f};
};

struct JointComponent {
    JointType type{JointType::Distance};
    EntityID target_entity{0};
    glm::vec2 local_anchor_a{0.0f, 0.0f};
    glm::vec2 local_anchor_b{0.0f, 0.0f};
    bool collide_connected{false};
    DistanceJointConfig distance_config;
    RevoluteJointConfig revolute_config;
    PrismaticJointConfig prismatic_config;

    JointComponent() = default;

    static JointComponent Distance(
        EntityID target,
        const glm::vec2& anchor_a = {0.0f, 0.0f},
        const glm::vec2& anchor_b = {0.0f, 0.0f},
        float length = -1.0f,
        float stiffness = 0.0f,
        float damping = 0.0f,
        bool collide = false
    ) {
        JointComponent joint;
        joint.type = JointType::Distance;
        joint.target_entity = target;
        joint.local_anchor_a = anchor_a;
        joint.local_anchor_b = anchor_b;
        joint.collide_connected = collide;
        joint.distance_config = {length, 0.0f, -1.0f, stiffness, damping};
        return joint;
    }

    static JointComponent Rope(
        EntityID target,
        float max_length = -1.0f,
        const glm::vec2& anchor_a = {0.0f, 0.0f},
        const glm::vec2& anchor_b = {0.0f, 0.0f},
        bool collide = false
    ) {
        JointComponent joint;
        joint.type = JointType::Rope;
        joint.target_entity = target;
        joint.local_anchor_a = anchor_a;
        joint.local_anchor_b = anchor_b;
        joint.collide_connected = collide;
        joint.distance_config.min_length = 0.0f;
        joint.distance_config.max_length = max_length;
        return joint;
    }

    static JointComponent Revolute(
        EntityID target,
        const glm::vec2& anchor_a = {0.0f, 0.0f},
        const glm::vec2& anchor_b = {0.0f, 0.0f},
        bool collide = false
    ) {
        JointComponent joint;
        joint.type = JointType::Revolute;
        joint.target_entity = target;
        joint.local_anchor_a = anchor_a;
        joint.local_anchor_b = anchor_b;
        joint.collide_connected = collide;
        return joint;
    }

    static JointComponent Prismatic(
        EntityID target,
        const glm::vec2& axis = {1.0f, 0.0f},
        const glm::vec2& anchor_a = {0.0f, 0.0f},
        const glm::vec2& anchor_b = {0.0f, 0.0f},
        bool collide = false
    ) {
        JointComponent joint;
        joint.type = JointType::Prismatic;
        joint.target_entity = target;
        joint.local_anchor_a = anchor_a;
        joint.local_anchor_b = anchor_b;
        joint.collide_connected = collide;
        joint.prismatic_config.axis = axis;
        return joint;
    }
};

} // namespace Engine
