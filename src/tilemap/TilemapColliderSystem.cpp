#include "TilemapColliderSystem.hpp"
#include "../ecs/Components.hpp"
#include "../core/Log.hpp"
#include <vector>

namespace Engine {

void TilemapColliderSystem::build_composite_colliders(EntityManager& em, EntityID entity_id) {
    if (!em.is_alive(entity_id) || !em.has_component<TilemapComponent>(entity_id) || !em.has_component<TransformComponent>(entity_id)) {
        return;
    }

    const auto& tilemap = em.get_component<TilemapComponent>(entity_id);
    const auto& tx = em.get_component<TransformComponent>(entity_id);

    // 0. Destroy previous composite colliders belonging to this tilemap entity
    if (em.has_component<HierarchyComponent>(entity_id)) {
        auto children_copy = em.get_component<HierarchyComponent>(entity_id).children;
        for (EntityID child_id : children_copy) {
            if (em.is_alive(child_id) && em.has_component<TagComponent>(child_id)) {
                const auto& tag_name = em.get_component<TagComponent>(child_id).tag;
                if (tag_name.find("Tilemap_Composite_Collider") != std::string::npos ||
                    tag_name.find("Collider_Box_") != std::string::npos) {
                    em.destroy_entity(child_id);
                }
            }
        }
    }

    // Also clean up any legacy unparented composite colliders
    std::vector<EntityID> legacy_colliders;
    em.for_each<TagComponent>([&](EntityID id, const TagComponent& tag) {
        if (tag.tag == "Tilemap_Composite_Collider") {
            if (!em.has_component<HierarchyComponent>(id) || em.get_component<HierarchyComponent>(id).parent == 0) {
                legacy_colliders.push_back(id);
            }
        }
    });
    for (EntityID old_id : legacy_colliders) {
        em.destroy_entity(old_id);
    }

    if (!tilemap.generate_colliders || tilemap.empty()) return;

    int min_x = 0, min_y = 0, max_x = 0, max_y = 0;
    tilemap.get_bounds(min_x, min_y, max_x, max_y);

    int w = max_x - min_x + 1;
    int h = max_y - min_y + 1;
    float tile_sz = tilemap.tile_size;

    std::vector<bool> visited(w * h, false);
    size_t merged_boxes_count = 0;

    // Greedy 2D Box Merging Algorithm
    for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
            int local_idx = (y - min_y) * w + (x - min_x);
            if (visited[local_idx]) continue;

            uint16_t tile_id = tilemap.get_tile(x, y);
            if (!tilemap.is_solid_tile(tile_id)) {
                visited[local_idx] = true;
                continue;
            }

            // 1. Expand horizontally (find maximum width of contiguous solid tiles)
            int box_w = 1;
            while (x + box_w <= max_x) {
                int next_idx = (y - min_y) * w + (x + box_w - min_x);
                uint16_t next_id = tilemap.get_tile(x + box_w, y);
                if (visited[next_idx] || !tilemap.is_solid_tile(next_id)) {
                    break;
                }
                box_w++;
            }

            // 2. Expand vertically (check if subsequent full rows of width box_w are solid)
            int box_h = 1;
            while (y + box_h <= max_y) {
                bool row_valid = true;
                for (int rx = 0; rx < box_w; ++rx) {
                    int check_idx = (y + box_h - min_y) * w + (x + rx - min_x);
                    uint16_t check_id = tilemap.get_tile(x + rx, y + box_h);
                    if (visited[check_idx] || !tilemap.is_solid_tile(check_id)) {
                        row_valid = false;
                        break;
                    }
                }
                if (row_valid) {
                    box_h++;
                } else {
                    break;
                }
            }

            // 3. Mark all tiles in (x..x+box_w-1, y..y+box_h-1) as visited
            for (int ry = 0; ry < box_h; ++ry) {
                for (int rx = 0; rx < box_w; ++rx) {
                    visited[(y + ry - min_y) * w + (x + rx - min_x)] = true;
                }
            }

            // 4. Calculate local-space bounds for merged box relative to tilemap origin (0,0)
            float start_local_x = static_cast<float>(x) * tile_sz;
            float start_local_y = - static_cast<float>(y) * tile_sz;

            float box_center_local_x = start_local_x + (static_cast<float>(box_w) * tile_sz * 0.5f);
            float box_center_local_y = start_local_y - (static_cast<float>(box_h) * tile_sz * 0.5f);
            glm::vec2 box_size(static_cast<float>(box_w) * tile_sz, static_cast<float>(box_h) * tile_sz);

            // Register merged static collider box as a child entity of the Tilemap
            EntityID col_entity = em.create_entity();
            em.add_component<TagComponent>(col_entity, TagComponent{"Tilemap_Composite_Collider #" + std::to_string(merged_boxes_count + 1)});
            em.add_component<TransformComponent>(col_entity, TransformComponent{glm::vec3(box_center_local_x, box_center_local_y, 0.0f)});
            
            ColliderComponent col_comp = tilemap.is_one_way 
                ? ColliderComponent::OneWayBox(box_size, glm::vec2(0.0f), tilemap.category_bits, tilemap.mask_bits)
                : ColliderComponent::Box(box_size, glm::vec2(0.0f), tilemap.is_trigger, tilemap.category_bits, tilemap.mask_bits);
            em.add_component<ColliderComponent>(col_entity, col_comp);

            RigidBodyComponent rb = RigidBodyComponent::Static();
            rb.friction = tilemap.friction;
            rb.restitution = tilemap.restitution;
            em.add_component<RigidBodyComponent>(col_entity, rb);
            em.set_parent(col_entity, entity_id);

            merged_boxes_count++;
        }
    }

    ENGINE_LOG_INFO("[TILEMAP] Merged solid tiles into {} composite Box2D colliders (parented to entity #{})", merged_boxes_count, entity_id);
}

} // namespace Engine
