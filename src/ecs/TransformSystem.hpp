#pragma once

#include "EntityManager.hpp"
#include "Components.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace Engine {

class TransformSystem {
public:
    // Update world transforms for all entities, evaluating parent-child hierarchy trees
    static void update(EntityManager& entity_manager) {
        // Step 1: Initialize all entities' world transforms to their local transforms
        entity_manager.for_each<TransformComponent>(
            [](EntityID id, TransformComponent& transform) {
                transform.world_position = transform.position;
                transform.world_scale = transform.scale;
                transform.world_rotation = transform.rotation;
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
                    update_entity_hierarchy(entity_manager, id, transform.world_position, transform.world_scale, transform.world_rotation, transform.world_visible);
                }
            }
        );
    }

private:
    static void update_entity_hierarchy(
        EntityManager& entity_manager, 
        EntityID parent_id, 
        const glm::vec3& parent_world_pos, 
        const glm::vec2& parent_world_scale, 
        float parent_world_rot,
        bool parent_world_vis
    ) {
        if (!entity_manager.has_component<HierarchyComponent>(parent_id)) return;

        const auto& hierarchy = entity_manager.get_component<HierarchyComponent>(parent_id);
        for (EntityID child_id : hierarchy.children) {
            if (!entity_manager.is_alive(child_id) || !entity_manager.has_component<TransformComponent>(child_id)) continue;

            auto& child_transform = entity_manager.get_component<TransformComponent>(child_id);

            // Rotate local position by parent world rotation
            float rad = glm::radians(parent_world_rot);
            float cos_a = std::cos(rad);
            float sin_a = std::sin(rad);

            glm::vec2 scaled_local_pos = glm::vec2(child_transform.position.x, child_transform.position.y) * parent_world_scale;
            float rotated_x = scaled_local_pos.x * cos_a - scaled_local_pos.y * sin_a;
            float rotated_y = scaled_local_pos.x * sin_a + scaled_local_pos.y * cos_a;

            child_transform.world_position = parent_world_pos + glm::vec3(rotated_x, rotated_y, child_transform.position.z);
            child_transform.world_scale = parent_world_scale * child_transform.scale;
            child_transform.world_rotation = parent_world_rot + child_transform.rotation;
            child_transform.world_visible = parent_world_vis && child_transform.visible;

            // Recurse down to grandchildren
            update_entity_hierarchy(entity_manager, child_id, child_transform.world_position, child_transform.world_scale, child_transform.world_rotation, child_transform.world_visible);
        }
    }
};

} // namespace Engine
