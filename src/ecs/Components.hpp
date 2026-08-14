#pragma once

#include "../renderer/Texture.hpp"
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

// Physics Collider Shapes (Box, Circle, Triangle)
enum class ColliderShapeType { Box, Circle, Triangle };

struct ColliderShape {
    ColliderShapeType type{ColliderShapeType::Box};
    glm::vec2 offset{0.0f, 0.0f};  // Offset relative to entity transform center
    glm::vec2 size{32.0f, 32.0f};   // Dimensions for Box shape
    float radius{16.0f};            // Radius for Circle shape
    glm::vec2 p1{0.0f, 0.0f};       // Vertex 1 for Triangle shape
    glm::vec2 p2{0.0f, 0.0f};       // Vertex 2 for Triangle shape
    glm::vec2 p3{0.0f, 0.0f};       // Vertex 3 for Triangle shape
    bool is_trigger{false};         // True = Trigger sensor (no solid physical response)
};

// Composite Collider Component supporting multiple shapes and triggers on a single entity
struct ColliderComponent {
    std::vector<ColliderShape> shapes;

    ColliderComponent() = default;

    // Helper static constructors for simple single-shape creation
    static ColliderComponent Box(const glm::vec2& size, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false) {
        ColliderComponent comp;
        comp.add_box(size, offset, is_trigger);
        return comp;
    }

    static ColliderComponent Circle(float radius, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false) {
        ColliderComponent comp;
        comp.add_circle(radius, offset, is_trigger);
        return comp;
    }

    static ColliderComponent Triangle(const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, bool is_trigger = false) {
        ColliderComponent comp;
        comp.add_triangle(p1, p2, p3, is_trigger);
        return comp;
    }

    // Chainable composite shape builders
    ColliderComponent& add_box(const glm::vec2& size, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false) {
        shapes.push_back({ColliderShapeType::Box, offset, size, 0.0f, {0,0}, {0,0}, {0,0}, is_trigger});
        return *this;
    }

    ColliderComponent& add_circle(float radius, const glm::vec2& offset = {0.0f, 0.0f}, bool is_trigger = false) {
        shapes.push_back({ColliderShapeType::Circle, offset, {0,0}, radius, {0,0}, {0,0}, {0,0}, is_trigger});
        return *this;
    }

    ColliderComponent& add_triangle(const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, bool is_trigger = false) {
        shapes.push_back({ColliderShapeType::Triangle, {0,0}, {0,0}, 0.0f, p1, p2, p3, is_trigger});
        return *this;
    }
};

// RigidBody Component for 2D dynamics, gravity, and velocity physics simulation
struct RigidBodyComponent {
    glm::vec2 velocity{0.0f, 0.0f};
    glm::vec2 acceleration{0.0f, 0.0f};
    float mass{1.0f};
    float gravity_scale{1.0f};      // 1.0f = Normal Gravity, 0.0f = Floating / Zero Gravity
    bool is_static{false};          // True = Immovable solid (Walls, Floors, Terrain)
    float restitution{0.2f};         // Elasticity / Bounciness factor (0.0 = No bounce, 1.0 = Perfect bounce)

    RigidBodyComponent() = default;
    explicit RigidBodyComponent(bool static_body, float gravity = 1.0f, float bounciness = 0.2f)
        : is_static(static_body), gravity_scale(static_body ? 0.0f : gravity), restitution(bounciness) {}
};

} // namespace Engine
