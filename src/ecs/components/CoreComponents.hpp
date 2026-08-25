#pragma once

#include "../../core/AssetHandle.hpp"
#include "../EcsTypes.hpp"
#include <algorithm>
#include <glm/glm.hpp>
#include <cmath>
#include <functional>
#include <string>
#include <string_view>
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

// Semantic gameplay identity shared by runtime queries, editor tooling, tests,
// and AI project discovery. Tags are canonical lowercase dotted identifiers
// such as "player" or "collectible.coin".
struct GameplayTagsComponent {
    std::vector<std::string> tags;

    [[nodiscard]] bool contains(const std::string& tag) const {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }

    bool operator==(const GameplayTagsComponent&) const = default;
};

[[nodiscard]] inline bool is_valid_gameplay_tag(std::string_view tag) {
    if (tag.empty() || tag.size() > 64 || tag.front() == '.' || tag.back() == '.') return false;
    bool previous_dot = false;
    for (const char character : tag) {
        const bool dot = character == '.';
        if (dot && previous_dot) return false;
        const bool lowercase_letter = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!dot && character != '_' && character != '-' && !lowercase_letter && !digit) return false;
        previous_dot = dot;
    }
    return true;
}

struct BehaviorComponent {
    std::function<void(EntityID, float)> update_fn;

    BehaviorComponent() = default;
    explicit BehaviorComponent(std::function<void(EntityID, float)> fn) : update_fn(std::move(fn)) {}
};

struct GroupComponent {};

struct PrefabLinkComponent {
    std::string prefab_asset_path;
    std::string base_prefab_path; // Non-empty if this instance is a Prefab Variant
    EntityID source_local_id{1};
    bool is_root_instance{true};

    PrefabLinkComponent() = default;
    explicit PrefabLinkComponent(
        std::string path,
        std::string base_path = "",
        EntityID source_id = 1,
        bool is_root = true
    ) : prefab_asset_path(std::move(path)), base_prefab_path(std::move(base_path)),
        source_local_id(source_id), is_root_instance(is_root) {}

    [[nodiscard]] bool is_variant() const { return !base_prefab_path.empty(); }
};


} // namespace Engine
