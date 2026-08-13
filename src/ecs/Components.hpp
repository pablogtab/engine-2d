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

// Transform Component for 2.5D positioning, depth scaling, and rotation
struct TransformComponent {
    glm::vec3 position{0.0f, 0.0f, 0.0f}; // Local X, Y, Z (Depth)
    glm::vec2 scale{1.0f, 1.0f};          // Local Scale
    float rotation{0.0f};                 // Local Angle in degrees
    bool visible{true};                   // Local Visibility flag

    // Computed World Transforms (updated by TransformSystem for parent-child hierarchies)
    glm::vec3 world_position{0.0f, 0.0f, 0.0f};
    glm::vec2 world_scale{1.0f, 1.0f};
    float world_rotation{0.0f};
    bool world_visible{true};             // Top-Down Hierarchical Computed Visibility

    TransformComponent() = default;
    TransformComponent(const glm::vec3& pos, const glm::vec2& scl = {1.0f, 1.0f}, float rot = 0.0f)
        : position(pos), scale(scl), rotation(rot), world_position(pos), world_scale(scl), world_rotation(rot) {}
    TransformComponent(float x, float y, float z = 0.0f)
        : position(x, y, z), scale(1.0f, 1.0f), rotation(0.0f), world_position(x, y, z), world_scale(1.0f, 1.0f), world_rotation(0.0f) {}
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

} // namespace Engine
