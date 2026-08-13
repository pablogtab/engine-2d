#pragma once

#include "../renderer/Texture.hpp"
#include <glm/glm.hpp>
#include <SDL3/SDL.h>
#include <string>
#include <functional>

namespace Engine {

using EntityID = uint32_t;

// Transform Component for 2.5D positioning, depth scaling, and rotation
struct TransformComponent {
    glm::vec3 position{0.0f, 0.0f, 0.0f}; // Local X, Y, Z (Depth)
    glm::vec2 scale{1.0f, 1.0f};          // Local Scale
    float rotation{0.0f};                 // Local Angle in degrees

    // Computed World Transforms (updated by TransformSystem for parent-child hierarchies)
    glm::vec3 world_position{0.0f, 0.0f, 0.0f};
    glm::vec2 world_scale{1.0f, 1.0f};
    float world_rotation{0.0f};

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
