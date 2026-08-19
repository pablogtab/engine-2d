#pragma once

#include "EntityManager.hpp"
#include "components/CoreComponents.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <optional>

namespace Engine {

class TransformSystem {
public:
    // Compute child world transform from child local transform and parent world transform
    [[nodiscard]] static TransformComponent local_to_world_transform(
        const TransformComponent& child_local,
        const TransformComponent* parent_world
    ) {
        if (!parent_world) {
            TransformComponent world = child_local;
            world.world_position = child_local.position;
            world.world_scale = child_local.scale;
            world.world_rotation = child_local.rotation;
            world.world_pivot = child_local.pivot;
            world.world_skew = child_local.skew;
            world.world_visible = child_local.visible;
            return world;
        }

        TransformComponent world = child_local;

        const float rad = glm::radians(parent_world->world_rotation);
        const float cos_a = std::cos(rad);
        const float sin_a = std::sin(rad);

        const glm::vec2 scaled_local_pos = glm::vec2(child_local.position.x, child_local.position.y) * parent_world->world_scale;
        const float rotated_x = scaled_local_pos.x * cos_a - scaled_local_pos.y * sin_a;
        const float rotated_y = scaled_local_pos.x * sin_a + scaled_local_pos.y * cos_a;

        world.world_position = parent_world->world_position + glm::vec3(rotated_x, rotated_y, child_local.position.z);
        world.world_scale = parent_world->world_scale * child_local.scale;
        world.world_rotation = parent_world->world_rotation + child_local.rotation;
        world.world_pivot = child_local.pivot;
        world.world_skew = parent_world->world_skew + child_local.skew;
        world.world_visible = parent_world->world_visible && child_local.visible;
        return world;
    }

    // Compute child local transform from desired world transform and parent world transform.
    // Returns std::nullopt if the parent's world scale is degenerate (near zero), making inversion impossible.
    [[nodiscard]] static std::optional<TransformComponent> world_to_local_transform(
        const TransformComponent& child_world,
        const TransformComponent* parent_world
    ) {
        if (!parent_world) {
            TransformComponent local = child_world;
            local.position = child_world.world_position;
            local.scale = child_world.world_scale;
            local.rotation = child_world.world_rotation;
            local.pivot = child_world.world_pivot;
            local.skew = child_world.world_skew;
            local.visible = child_world.world_visible;
            return local;
        }

        if (std::abs(parent_world->world_scale.x) < 0.0001f || std::abs(parent_world->world_scale.y) < 0.0001f) {
            return std::nullopt;
        }

        TransformComponent local = child_world;

        const glm::vec3 delta_pos = child_world.world_position - parent_world->world_position;
        const float rad = glm::radians(-parent_world->world_rotation);
        const float cos_a = std::cos(rad);
        const float sin_a = std::sin(rad);

        const float unrot_x = delta_pos.x * cos_a - delta_pos.y * sin_a;
        const float unrot_y = delta_pos.x * sin_a + delta_pos.y * cos_a;

        local.position = glm::vec3(
            unrot_x / parent_world->world_scale.x,
            unrot_y / parent_world->world_scale.y,
            delta_pos.z
        );

        local.scale = glm::vec2(
            child_world.world_scale.x / parent_world->world_scale.x,
            child_world.world_scale.y / parent_world->world_scale.y
        );

        local.rotation = child_world.world_rotation - parent_world->world_rotation;
        local.skew = child_world.world_skew - parent_world->world_skew;
        local.pivot = child_world.world_pivot;
        local.visible = child_world.visible;

        return local;
    }

    // Helper to calculate local transform when changing parent for an entity in the EntityManager.
    // Returns std::nullopt if the conversion cannot preserve the world transform (e.g. degenerate parent scale).
    [[nodiscard]] static std::optional<TransformComponent> calculate_reparent_local_transform(
        EntityManager& entity_manager,
        EntityID child,
        EntityID new_parent
    ) {
        if (!entity_manager.is_alive(child) || !entity_manager.has_component<TransformComponent>(child)) {
            return std::nullopt;
        }

        update(entity_manager);
        const auto& child_t = entity_manager.get_component<TransformComponent>(child);

        if (new_parent == 0) {
            return world_to_local_transform(child_t, nullptr);
        }

        if (!entity_manager.is_alive(new_parent) || !entity_manager.has_component<TransformComponent>(new_parent)) {
            return std::nullopt;
        }

        const auto& parent_t = entity_manager.get_component<TransformComponent>(new_parent);
        return world_to_local_transform(child_t, &parent_t);
    }

    // Update world transforms for all entities, evaluating parent-child hierarchy trees
    static void update(EntityManager& entity_manager) {
        // Step 1: Initialize all entities' world transforms to their local transforms
        entity_manager.for_each<TransformComponent>(
            [](EntityID id, TransformComponent& transform) {
                (void)id;
                transform.world_position = transform.position;
                transform.world_scale = transform.scale;
                transform.world_rotation = transform.rotation;
                transform.world_pivot = transform.pivot;
                transform.world_skew = transform.skew;
                transform.world_visible = transform.visible;
            }
        );

        // Step 2: Traverse root entities (entities without a parent) and propagate transforms to children
        entity_manager.for_each<TransformComponent>(
            [&entity_manager](EntityID id, TransformComponent& transform) {
                bool has_valid_parent = false;
                if (entity_manager.has_component<HierarchyComponent>(id)) {
                    EntityID parent_id = entity_manager.get_component<HierarchyComponent>(id).parent;
                    if (parent_id != 0 && entity_manager.is_alive(parent_id)) {
                        has_valid_parent = true;
                    }
                }

                if (!has_valid_parent) {
                    update_entity_hierarchy(entity_manager, id, transform);
                }
            }
        );
    }

private:
    static void update_entity_hierarchy(
        EntityManager& entity_manager, 
        EntityID parent_id, 
        const TransformComponent& parent_world
    ) {
        if (!entity_manager.has_component<HierarchyComponent>(parent_id)) return;

        const auto& hierarchy = entity_manager.get_component<HierarchyComponent>(parent_id);
        for (EntityID child_id : hierarchy.children) {
            if (!entity_manager.is_alive(child_id) || !entity_manager.has_component<TransformComponent>(child_id)) continue;

            auto& child_transform = entity_manager.get_component<TransformComponent>(child_id);
            child_transform = local_to_world_transform(child_transform, &parent_world);

            // Recurse down to grandchildren
            update_entity_hierarchy(entity_manager, child_id, child_transform);
        }
    }
};

} // namespace Engine
