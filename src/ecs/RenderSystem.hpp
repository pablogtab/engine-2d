#pragma once

#include "EntityManager.hpp"
#include "Components.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../scene/SpatialGrid.hpp"
#include <algorithm>
#include <vector>

namespace Engine {

class RenderSystem {
public:
    // Render all entities possessing TransformComponent and SpriteComponent with 0-overhead Spatial Grid & Top-Down visibility
    static void render(EntityManager& entity_manager, Renderer2D& renderer) {
        // Step 1: Re-populate Spatial Hash Grid with living renderable entities
        static SpatialGrid s_spatial_grid(256.0f);
        s_spatial_grid.clear();

        entity_manager.for_each<TransformComponent, SpriteComponent>(
            [](EntityID id, const TransformComponent& transform, const SpriteComponent& sprite) {
                if (transform.world_visible && sprite.visible && sprite.texture) {
                    s_spatial_grid.insert(id, transform.world_position, sprite.size * transform.world_scale);
                }
            }
        );

        // Step 2: Query candidate entities within camera bounds (or fallback to full set if no camera active)
        const Camera2D* active_cam = renderer.get_active_camera();
        if (active_cam) {
            SDL_FRect cam_bounds = active_cam->get_world_bounds();
            static std::vector<EntityID> visible_candidates;
            visible_candidates.clear();
            s_spatial_grid.query_bounds(cam_bounds, visible_candidates);

            // Deduplicate candidate IDs since large entities can overlap multiple grid cells
            std::sort(visible_candidates.begin(), visible_candidates.end());
            visible_candidates.erase(std::unique(visible_candidates.begin(), visible_candidates.end()), visible_candidates.end());

            for (EntityID id : visible_candidates) {
                if (!entity_manager.has_component<TransformComponent>(id) || !entity_manager.has_component<SpriteComponent>(id)) continue;
                const auto& transform = entity_manager.get_component<TransformComponent>(id);
                const auto& sprite = entity_manager.get_component<SpriteComponent>(id);

                if (!transform.world_visible || !sprite.visible || !sprite.texture) continue;

                const SDL_FRect* src_rect_ptr = sprite.use_src_rect ? &sprite.src_rect : nullptr;

                renderer.submit_sprite(
                    sprite.texture,
                    transform.world_position,
                    sprite.size * transform.world_scale,
                    transform.world_rotation,
                    sprite.color,
                    src_rect_ptr,
                    transform.world_pivot
                );
            }
        } else {
            // Fallback for un-camera-bound rendering
            entity_manager.for_each<TransformComponent, SpriteComponent>(
                [&renderer](EntityID id, const TransformComponent& transform, const SpriteComponent& sprite) {
                    if (!transform.world_visible || !sprite.visible || !sprite.texture) return;

                    const SDL_FRect* src_rect_ptr = sprite.use_src_rect ? &sprite.src_rect : nullptr;

                    renderer.submit_sprite(
                        sprite.texture,
                        transform.world_position,
                        sprite.size * transform.world_scale,
                        transform.world_rotation,
                        sprite.color,
                        src_rect_ptr,
                        transform.world_pivot
                    );
                }
            );
        }
    }
};

} // namespace Engine
