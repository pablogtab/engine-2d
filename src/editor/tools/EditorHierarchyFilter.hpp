#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../ecs/components/RenderingComponents.hpp"
#include "../../ecs/components/PhysicsComponents.hpp"
#include "../../ecs/components/AnimationComponents.hpp"
#include "../../tilemap/Tilemap.hpp"
#include "../state/EditorEntityStateStore.hpp"
#include <string>
#include <algorithm>
#include <cstddef>
#include <cctype>

namespace Engine {

class EditorHierarchyFilter {
public:
    [[nodiscard]] static bool matches_single(
        const EntityManager& em,
        EntityID id,
        const std::string& filter,
        const EditorEntityStateStore* state_store = nullptr
    ) {
        if (filter.empty()) return true;

        std::string type_filter = filter;
        if (type_filter.rfind("t:", 0) == 0) {
            type_filter = type_filter.substr(2);
        } else if (type_filter.rfind(":", 0) == 0) {
            type_filter = type_filter.substr(1);
        }

        if (type_filter == "sprite" || type_filter == "spr") {
            return em.has_component<SpriteComponent>(id);
        }
        if (type_filter == "collider" || type_filter == "col") {
            return em.has_component<ColliderComponent>(id);
        }
        if (type_filter == "rigidbody" || type_filter == "rb") {
            return em.has_component<RigidBodyComponent>(id);
        }
        if (type_filter == "camera" || type_filter == "cam") {
            return em.has_component<CameraComponent>(id);
        }
        if (type_filter == "tilemap" || type_filter == "tile") {
            return em.has_component<TilemapComponent>(id);
        }
        if (type_filter == "anim" || type_filter == "animation") {
            return em.has_component<AnimationComponent>(id);
        }
        if (type_filter == "locked" || type_filter == "lock") {
            return state_store && state_store->is_locked(id);
        }
        if (type_filter == "hidden" || type_filter == "hide") {
            return state_store && !state_store->is_visible(id);
        }

        // Default: name / tag substring match
        std::string label = "Entity";
        if (em.has_component<TagComponent>(id)) {
            label = em.get_component<TagComponent>(id).tag;
        }
        std::string lower_label = label;
        std::transform(
            lower_label.begin(), lower_label.end(), lower_label.begin(),
            [](unsigned char character) { return static_cast<char>(std::tolower(character)); }
        );
        return lower_label.find(filter) != std::string::npos;
    }

    [[nodiscard]] static bool matches_recursive(
        const EntityManager& em,
        EntityID id,
        const std::string& filter_str,
        const EditorEntityStateStore* state_store = nullptr
    ) {
        if (filter_str.empty()) return true;

        if (matches_single(em, id, filter_str, state_store)) {
            return true;
        }

        if (em.has_component<HierarchyComponent>(id)) {
            for (EntityID child_id : em.get_component<HierarchyComponent>(id).children) {
                if (em.is_alive(child_id) && matches_recursive(em, child_id, filter_str, state_store)) {
                    return true;
                }
            }
        }
        return false;
    }

    [[nodiscard]] static std::size_t count_matches(
        const EntityManager& em,
        const std::string& filter,
        const EditorEntityStateStore* state_store = nullptr
    ) {
        if (filter.empty()) return em.get_living_entities().size();

        std::size_t count = 0;
        for (EntityID id : em.get_living_entities()) {
            if (em.is_alive(id) && matches_single(em, id, filter, state_store)) {
                ++count;
            }
        }
        return count;
    }
};

} // namespace Engine
