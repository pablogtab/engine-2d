#pragma once

#include "EntityManager.hpp"
#include "Components.hpp"
#include "../renderer/Renderer2D.hpp"

namespace Engine {

class RenderSystem {
public:
    // Render all entities possessing TransformComponent and SpriteComponent with 0-overhead Top-Down visibility
    static void render(EntityManager& entity_manager, Renderer2D& renderer) {
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
                    src_rect_ptr
                );
            }
        );
    }
};

} // namespace Engine
