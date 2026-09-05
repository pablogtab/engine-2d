#include "TilemapColliderSystem.hpp"
#include "../ecs/components/CoreComponents.hpp"
#include "../ecs/components/PhysicsComponents.hpp"
#include "../core/Log.hpp"
#include <vector>
#include <cmath>

namespace Engine {

void TilemapColliderSystem::clear_composite_colliders(EntityManager& em, EntityID entity_id) {
    std::vector<EntityID> generated_colliders;
    em.for_each<TilemapGeneratedColliderComponent>([&](EntityID id, const TilemapGeneratedColliderComponent& generated) {
        if (generated.owner == entity_id) {
            generated_colliders.push_back(id);
        }
    });
    for (EntityID collider : generated_colliders) {
        em.destroy_entity(collider);
    }
}

void TilemapColliderSystem::build_composite_colliders(EntityManager& em, EntityID entity_id) {
    clear_composite_colliders(em, entity_id);
    if (!em.is_alive(entity_id) || !em.has_component<TilemapComponent>(entity_id) || !em.has_component<TransformComponent>(entity_id)) {
        return;
    }

    const auto& tilemap = em.get_component<TilemapComponent>(entity_id);

    if (!tilemap.generate_colliders || tilemap.empty()) return;

    const float tile_sz = tilemap.tile_size;
    if (!std::isfinite(tile_sz) || tile_sz <= 0) return;
    std::unordered_set<int64_t> remaining;
    std::vector<std::pair<int, int>> cells;
    for (const auto& [key, value] : tilemap.tiles) {
        if (!tilemap.is_solid_tile(value)) continue;
        int x, y;
        TilemapComponent::parse_key(key, x, y);
        remaining.insert(key);
        cells.emplace_back(y, x);
    }
    std::sort(cells.begin(), cells.end());
    const auto contains = [&](int64_t x, int64_t y) {
        return x >= std::numeric_limits<int>::min() && x <= std::numeric_limits<int>::max() &&
            y >= std::numeric_limits<int>::min() && y <= std::numeric_limits<int>::max() &&
            remaining.contains(TilemapComponent::make_key(static_cast<int>(x), static_cast<int>(y)));
    };
    size_t merged_boxes_count = 0;
    // Visit occupied cells only; empty distance never drives allocation or work.
    for (const auto& [cell_y, cell_x] : cells) {
            const int64_t x = cell_x, y = cell_y;
            if (!contains(x, y)) continue;
            int64_t box_w = 1, box_h = 1;
            while (contains(x + box_w, y)) ++box_w;
            for (;;) {
                bool full_row = true;
                for (int64_t rx = 0; rx < box_w; ++rx) {
                    if (!contains(x + rx, y + box_h)) { full_row = false; break; }
                }
                if (!full_row) break;
                ++box_h;
            }
            for (int64_t ry = 0; ry < box_h; ++ry)
                for (int64_t rx = 0; rx < box_w; ++rx)
                    remaining.erase(TilemapComponent::make_key(static_cast<int>(x + rx), static_cast<int>(y + ry)));

            // 4. Calculate local-space bounds for merged box relative to tilemap origin (0,0)
            float start_local_x = static_cast<float>(x) * tile_sz;
            float start_local_y = - static_cast<float>(y) * tile_sz;

            float box_center_local_x = start_local_x + (static_cast<float>(box_w) * tile_sz * 0.5f);
            float box_center_local_y = start_local_y - (static_cast<float>(box_h) * tile_sz * 0.5f);
            glm::vec2 box_size(static_cast<float>(box_w) * tile_sz, static_cast<float>(box_h) * tile_sz);

            // Register merged static collider box as a child entity of the Tilemap
            EntityID col_entity = em.create_entity();
            em.add_component<TagComponent>(col_entity, TagComponent{"Tilemap_Composite_Collider #" + std::to_string(merged_boxes_count + 1)});
            em.add_component<TilemapGeneratedColliderComponent>(col_entity, TilemapGeneratedColliderComponent{entity_id});
            em.add_component<TransformComponent>(col_entity, TransformComponent{glm::vec3(box_center_local_x, box_center_local_y, 0.0f)});
            
            ColliderComponent col_comp = tilemap.is_one_way 
                ? ColliderComponent::OneWayBox(box_size, glm::vec2(0.0f), tilemap.category_bits, tilemap.mask_bits)
                : ColliderComponent::Box(box_size, glm::vec2(0.0f), tilemap.is_trigger, tilemap.category_bits, tilemap.mask_bits);
            em.add_component<ColliderComponent>(col_entity, col_comp);

            RigidBodyComponent rb = RigidBodyComponent::Static();
            rb.friction = tilemap.friction;
            rb.restitution = tilemap.restitution;
            em.add_component<RigidBodyComponent>(col_entity, rb);
            const HierarchyOperationResult hierarchy = em.set_parent(col_entity, entity_id);
            if (!hierarchy.succeeded()) {
                ENGINE_LOG_ERROR(
                    "[TILEMAP] Cannot parent generated collider #{} to entity #{}: {}",
                    col_entity,
                    entity_id,
                    hierarchy.message()
                );
                em.destroy_entity(col_entity);
                continue;
            }

            merged_boxes_count++;
    }

    ENGINE_LOG_INFO("[TILEMAP] Merged solid tiles into {} composite Box2D colliders (parented to entity #{})", merged_boxes_count, entity_id);
}

} // namespace Engine
