#pragma once

#include "../renderer/Texture.hpp"
#include "../physics/BezierCurve.hpp"
#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <string>
#include <functional>
#include <bitset>

namespace Engine {

using EntityID = uint32_t;
using ComponentTypeID = uint8_t;
static constexpr size_t MAX_COMPONENTS = 64;
using ComponentMask = std::bitset<MAX_COMPONENTS>;

// Helper function to assign a unique 0..63 ID per Component type
inline ComponentTypeID get_next_type_id() {
    static ComponentTypeID last_id = 0;
    return last_id++;
}

template<typename T>
inline ComponentTypeID get_component_type_id() {
    static ComponentTypeID type_id = get_next_type_id();
    return type_id;
}

// Exclusion tag helper for ECS queries: for_each(exclude<Tag1, Tag2>, func)
template<typename... Ts>
struct exclude_t {};

template<typename... Ts>
inline constexpr exclude_t<Ts...> exclude{};

// Transform Component for 2.5D positioning, depth scaling, rotation, pivot, and skew
struct TransformComponent {
    glm::vec3 position{0.0f, 0.0f, 0.0f}; // Local X, Y, Z (Depth)
    glm::vec2 scale{1.0f, 1.0f};          // Local Scale
    float rotation{0.0f};                 // Local Angle in degrees
    glm::vec2 pivot{0.5f, 0.5f};          // Normalized Pivot Anchor: (0.5, 0.5) = Center, (0.5, 0.0) = Bottom, (0, 0) = Top-Left
    glm::vec2 skew{0.0f, 0.0f};           // Shear / Skew angles in degrees (Squash & Stretch, pseudo-3D shadows)
    bool visible{true};                   // Local Visibility flag

    // Computed World Transforms (updated by TransformSystem for parent-child hierarchies)
    glm::vec3 world_position{0.0f, 0.0f, 0.0f};
    glm::vec2 world_scale{1.0f, 1.0f};
    float world_rotation{0.0f};
    glm::vec2 world_pivot{0.5f, 0.5f};
    glm::vec2 world_skew{0.0f, 0.0f};
    bool world_visible{true};             // Top-Down Hierarchical Computed Visibility

    TransformComponent() = default;
    TransformComponent(const glm::vec3& pos, const glm::vec2& scl = {1.0f, 1.0f}, float rot = 0.0f, const glm::vec2& pvt = {0.5f, 0.5f})
        : position(pos), scale(scl), rotation(rot), pivot(pvt), world_position(pos), world_scale(scl), world_rotation(rot), world_pivot(pvt) {}
    TransformComponent(float x, float y, float z = 0.0f)
        : position(x, y, z), scale(1.0f, 1.0f), rotation(0.0f), pivot(0.5f, 0.5f), world_position(x, y, z), world_scale(1.0f, 1.0f), world_rotation(0.0f), world_pivot(0.5f, 0.5f) {}

    // Convenience Helper: Rotate transform to face target world position
    void look_at(const glm::vec2& target_world_pos) {
        glm::vec2 dir = target_world_pos - glm::vec2(world_position.x, world_position.y);
        if (glm::length(dir) > 0.0001f) {
            rotation = glm::degrees(std::atan2(dir.y, dir.x));
        }
    }

    // Convenience Helper: Get normalized right direction vector
    glm::vec2 get_right() const {
        float rad = glm::radians(world_rotation);
        return glm::vec2(std::cos(rad), std::sin(rad));
    }

    // Convenience Helper: Get normalized up direction vector
    glm::vec2 get_up() const {
        float rad = glm::radians(world_rotation);
        return glm::vec2(-std::sin(rad), std::cos(rad));
    }
};

// Hierarchy Component for Parent-Child entity relationships
struct HierarchyComponent {
    EntityID parent{0};
    std::vector<EntityID> children;

    HierarchyComponent() = default;
    explicit HierarchyComponent(EntityID parent_id) : parent(parent_id) {}
};

// Sprite Component for rendering visual assets
struct SpriteComponent {
    Texture* texture{nullptr};
    SDL_Color color{255, 255, 255, 255};
    SDL_FRect src_rect{0.0f, 0.0f, 0.0f, 0.0f};
    bool use_src_rect{false};
    bool visible{true};
    glm::vec2 size{64.0f, 64.0f};

    SpriteComponent() = default;
    explicit SpriteComponent(Texture* tex, const glm::vec2& sz = {64.0f, 64.0f}, SDL_Color col = {255, 255, 255, 255})
        : texture(tex), color(col), size(sz) {}
};

// Tag Component for naming/identifying entities
struct TagComponent {
    std::string tag{"Entity"};

    TagComponent() = default;
    explicit TagComponent(const std::string& name) : tag(name) {}
};

// Behavior Component for attaching dynamic logic/scripts to an entity
struct BehaviorComponent {
    std::function<void(EntityID, float)> update_fn;

    BehaviorComponent() = default;
    explicit BehaviorComponent(std::function<void(EntityID, float)> fn) : update_fn(fn) {}
};

namespace CollisionCategory {
    constexpr uint16_t NONE    = 0x0000;
    constexpr uint16_t DEFAULT = 0x0001; // 1 << 0 (Default layer)
    constexpr uint16_t PLAYER  = 0x0002; // 1 << 1 (Player character)
    constexpr uint16_t GROUND  = 0x0004; // 1 << 2 (Platforms, floors, walls)
    constexpr uint16_t COIN    = 0x0008; // 1 << 3 (Collectibles)
    constexpr uint16_t ENEMY   = 0x0010; // 1 << 4 (Enemies & Hazards)
    constexpr uint16_t ALL     = 0xFFFF; // Collides with everything
}

// Physics Collider Shapes (Box, Circle, Polygon / Slope)
enum class ColliderShapeType { Box, Circle, Polygon };

struct ColliderShape {
    ColliderShapeType type{ColliderShapeType::Box};
    glm::vec2 offset{0.0f, 0.0f};  // Offset relative to entity transform center
    glm::vec2 size{32.0f, 32.0f};   // Dimensions for Box shape
    float radius{16.0f};            // Radius for Circle shape
    std::vector<glm::vec2> vertices; // Local vertex array for Polygon / Slope shapes (up to 8 in Box2D)
    bool is_trigger{false};         // True = Trigger sensor (no solid physical response)
    bool is_one_way{false};         // True = Pass through from below, solid from above (One-Way Platform)
    uint16_t category_bits{CollisionCategory::DEFAULT}; // Layer this shape belongs to
    uint16_t mask_bits{CollisionCategory::ALL};         // Mask of layers this shape can collide with
};

// Composite Collider Component supporting multiple shapes and triggers on a single entity
struct ColliderComponent {
    std::vector<ColliderShape> shapes;

    ColliderComponent() = default;

    // Helper static constructors for simple single-shape creation
    static ColliderComponent Box(const glm::vec2& size, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        ColliderComponent comp;
        comp.add_box(size, offset, is_trigger, category, mask);
        return comp;
    }

    static ColliderComponent OneWayBox(const glm::vec2& size, const glm::vec2& offset = {0.0f, 0.0f}, uint16_t category = CollisionCategory::GROUND, uint16_t mask = CollisionCategory::ALL) {
        ColliderComponent comp;
        comp.add_one_way_box(size, offset, category, mask);
        return comp;
    }

    static ColliderComponent Circle(float radius, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        ColliderComponent comp;
        comp.add_circle(radius, offset, is_trigger, category, mask);
        return comp;
    }

    static ColliderComponent Polygon(const std::vector<glm::vec2>& vertices, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        ColliderComponent comp;
        comp.add_polygon(vertices, offset, is_trigger, category, mask);
        return comp;
    }

    static ColliderComponent Slope(float width, float height, bool slope_up_right = true, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        ColliderComponent comp;
        comp.add_slope(width, height, slope_up_right, offset, is_trigger, category, mask);
        return comp;
    }

    // Chainable composite shape builders
    ColliderComponent& add_box(const glm::vec2& size, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        shapes.push_back({ColliderShapeType::Box, offset, size, 0.0f, {}, is_trigger, false, category, mask});
        return *this;
    }

    ColliderComponent& add_one_way_box(const glm::vec2& size, const glm::vec2& offset = {0.0f, 0.0f}, uint16_t category = CollisionCategory::GROUND, uint16_t mask = CollisionCategory::ALL) {
        shapes.push_back({ColliderShapeType::Box, offset, size, 0.0f, {}, false, true, category, mask});
        return *this;
    }

    ColliderComponent& add_circle(float radius, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        shapes.push_back({ColliderShapeType::Circle, offset, {0,0}, radius, {}, is_trigger, false, category, mask});
        return *this;
    }

    ColliderComponent& add_polygon(const std::vector<glm::vec2>& vertices, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        shapes.push_back({ColliderShapeType::Polygon, offset, {0,0}, 0.0f, vertices, is_trigger, false, category, mask});
        return *this;
    }

    ColliderComponent& add_slope(float width, float height, bool slope_up_right = true, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false, uint16_t category = CollisionCategory::DEFAULT, uint16_t mask = CollisionCategory::ALL) {
        float hw = width * 0.5f;
        float hh = height * 0.5f;
        std::vector<glm::vec2> verts;
        if (slope_up_right) {
            // Right-triangle ramping UP from left to right: (-hw, -hh), (hw, -hh), (hw, hh)
            verts = {{-hw, -hh}, {hw, -hh}, {hw, hh}};
        } else {
            // Right-triangle ramping UP from right to left: (-hw, -hh), (hw, -hh), (-hw, hh)
            verts = {{-hw, -hh}, {hw, -hh}, {-hw, hh}};
        }
        return add_polygon(verts, offset, is_trigger, category, mask);
    }
};

enum class ForceMode {
    Impulse,         // 1-frame instant velocity change J = m * delta_v
    ContinuousForce, // Constant force F = m * a over duration T
    VelocityChange,  // Direct velocity override
    CurveOverTime    // Envelope magnitude F(t) = F_max * BezierCurve.evaluate(t / T)
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
    Dynamic,   // Affected by gravity, forces, and collisions (Player, rolling crates, debris)
    Static,    // Immovable solid (Ground, walls, level terrain)
    Kinematic  // Moving by script velocity, unaffected by gravity or forces, pushes dynamic bodies (Elevators, moving platforms)
};

// RigidBody Component for 2D dynamics, gravity, friction, and velocity physics simulation
struct RigidBodyComponent {
    BodyType body_type{BodyType::Dynamic};
    glm::vec2 velocity{0.0f, 0.0f};
    glm::vec2 acceleration{0.0f, 0.0f};
    float angular_velocity{0.0f};   // Rotational velocity (Degrees/sec)
    float mass{1.0f};
    float gravity_scale{1.0f};      // 1.0f = Normal Gravity, 0.0f = Floating / Zero Gravity
    float friction{0.4f};           // Surface friction coefficient (0.0 = Ice, 1.0 = High grip)
    bool fixed_rotation{true};     // True = Locked upright (Platformer player), False = Free physics rotation (rolling balls, debris)
    float restitution{0.2f};         // Elasticity / Bounciness factor (0.0 = No bounce, 1.0 = Perfect bounce)

    std::vector<ActiveForce> active_forces; // Queue of active force curve envelopes

    RigidBodyComponent() = default;
    explicit RigidBodyComponent(BodyType type, float gravity = 1.0f, float bounciness = 0.2f, float surface_friction = 0.4f, bool lock_rotation = true)
        : body_type(type), fixed_rotation(lock_rotation), gravity_scale((type == BodyType::Static || type == BodyType::Kinematic) ? 0.0f : gravity), restitution(bounciness), friction(surface_friction) {}

    static RigidBodyComponent Static(float surface_friction = 0.4f, float bounciness = 0.0f) {
        return RigidBodyComponent(BodyType::Static, 0.0f, bounciness, surface_friction, true);
    }

    static RigidBodyComponent Dynamic(float gravity = 1.0f, float bounciness = 0.2f, float surface_friction = 0.4f, bool lock_rotation = true) {
        return RigidBodyComponent(BodyType::Dynamic, gravity, bounciness, surface_friction, lock_rotation);
    }

    static RigidBodyComponent Kinematic(float surface_friction = 0.8f) {
        return RigidBodyComponent(BodyType::Kinematic, 0.0f, 0.0f, surface_friction, true);
    }
};

// 2D Physics Joints & Constraints
enum class JointType {
    Distance,   // Fixed distance / spring damper (rigid rods, springs)
    Rope,       // Flexible slack rope (slack when d < max_length, tense when d == max_length)
    Revolute,   // Hinge / Pin / Pendulum pivot (wheels, swinging chains, ragdolls)
    Prismatic   // Slider / Piston along an axis (elevators, pistons, sliding doors)
};

struct DistanceJointConfig {
    float length{-1.0f};      // Length in pixels (-1.0f = auto-compute initial distance)
    float min_length{0.0f};   // Minimum distance in pixels (0.0 for slack rope)
    float max_length{-1.0f};  // Maximum distance in pixels (-1.0 = auto)
    float stiffness{0.0f};   // Spring stiffness (Hz frequency in Box2D)
    float damping{0.0f};     // Damping ratio [0, 1]
};

struct RevoluteJointConfig {
    bool enable_limit{false};
    float lower_angle{0.0f};      // Lower limit in degrees
    float upper_angle{0.0f};      // Upper limit in degrees
    bool enable_motor{false};
    float motor_speed{0.0f};      // Desired motor speed in deg/sec
    float max_motor_torque{0.0f}; // Max motor torque
};

struct PrismaticJointConfig {
    glm::vec2 axis{1.0f, 0.0f};   // Direction of slide in local coordinates
    bool enable_limit{false};
    float lower_translation{0.0f}; // Lower translation limit in pixels
    float upper_translation{0.0f}; // Upper translation limit in pixels
    bool enable_motor{false};
    float motor_speed{0.0f};       // Desired motor speed in pixels/sec
    float max_motor_force{0.0f};  // Max motor force
};

struct JointComponent {
    JointType type{JointType::Distance};
    EntityID target_entity{0};           // The other entity to connect to
    glm::vec2 local_anchor_a{0.0f, 0.0f}; // Anchor on this entity (pixels relative to center)
    glm::vec2 local_anchor_b{0.0f, 0.0f}; // Anchor on target entity (pixels relative to center)
    bool collide_connected{false};       // True = Allow the two connected bodies to collide with each other

    DistanceJointConfig distance_config;
    RevoluteJointConfig revolute_config;
    PrismaticJointConfig prismatic_config;

    JointComponent() = default;

    static JointComponent Distance(EntityID target, const glm::vec2& anchor_a = {0.0f, 0.0f}, const glm::vec2& anchor_b = {0.0f, 0.0f},
                                   float length = -1.0f, float stiffness = 0.0f, float damping = 0.0f, bool collide = false) {
        JointComponent j;
        j.type = JointType::Distance;
        j.target_entity = target;
        j.local_anchor_a = anchor_a;
        j.local_anchor_b = anchor_b;
        j.collide_connected = collide;
        j.distance_config = {length, 0.0f, -1.0f, stiffness, damping};
        return j;
    }

    static JointComponent Rope(EntityID target, float max_length = -1.0f, const glm::vec2& anchor_a = {0.0f, 0.0f},
                               const glm::vec2& anchor_b = {0.0f, 0.0f}, bool collide = false) {
        JointComponent j;
        j.type = JointType::Rope;
        j.target_entity = target;
        j.local_anchor_a = anchor_a;
        j.local_anchor_b = anchor_b;
        j.collide_connected = collide;
        j.distance_config.min_length = 0.0f;
        j.distance_config.max_length = max_length;
        return j;
    }

    static JointComponent Revolute(EntityID target, const glm::vec2& anchor_a = {0.0f, 0.0f}, const glm::vec2& anchor_b = {0.0f, 0.0f},
                                   bool collide = false) {
        JointComponent j;
        j.type = JointType::Revolute;
        j.target_entity = target;
        j.local_anchor_a = anchor_a;
        j.local_anchor_b = anchor_b;
        j.collide_connected = collide;
        return j;
    }

    static JointComponent Prismatic(EntityID target, const glm::vec2& axis = {1.0f, 0.0f}, const glm::vec2& anchor_a = {0.0f, 0.0f},
                                    const glm::vec2& anchor_b = {0.0f, 0.0f}, bool collide = false) {
        JointComponent j;
        j.type = JointType::Prismatic;
        j.target_entity = target;
        j.local_anchor_a = anchor_a;
        j.local_anchor_b = anchor_b;
        j.collide_connected = collide;
        j.prismatic_config.axis = axis;
        return j;
    }
};

} // namespace Engine
