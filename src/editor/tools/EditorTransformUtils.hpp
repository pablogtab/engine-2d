#pragma once

#include "../../ecs/Components.hpp"
#include "../../ecs/EntityManager.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace Engine {

inline bool has_selected_ancestor(EntityManager& entity_manager, EntityID id, const std::unordered_set<EntityID>& selected_entities) {
    while (entity_manager.has_component<HierarchyComponent>(id)) {
        const EntityID parent = entity_manager.get_component<HierarchyComponent>(id).parent;
        if (parent == 0 || !entity_manager.is_alive(parent)) return false;
        if (selected_entities.contains(parent)) return true;
        id = parent;
    }
    return false;
}

inline glm::vec2 world_delta_to_local(EntityManager& entity_manager, EntityID id, const glm::vec2& world_delta) {
    if (!entity_manager.has_component<HierarchyComponent>(id)) return world_delta;

    const EntityID parent = entity_manager.get_component<HierarchyComponent>(id).parent;
    if (parent == 0 || !entity_manager.is_alive(parent) || !entity_manager.has_component<TransformComponent>(parent)) return world_delta;

    const auto& parent_transform = entity_manager.get_component<TransformComponent>(parent);
    const float radians = glm::radians(-parent_transform.world_rotation);
    const float cosine = std::cos(radians);
    const float sine = std::sin(radians);
    glm::vec2 local_delta(
        world_delta.x * cosine - world_delta.y * sine,
        world_delta.x * sine + world_delta.y * cosine
    );
    local_delta.x /= std::max(0.0001f, std::abs(parent_transform.world_scale.x));
    local_delta.y /= std::max(0.0001f, std::abs(parent_transform.world_scale.y));
    return local_delta;
}

} // namespace Engine
