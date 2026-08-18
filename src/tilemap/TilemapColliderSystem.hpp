#pragma once

#include "Tilemap.hpp"
#include "../ecs/EntityManager.hpp"

namespace Engine {

class TilemapColliderSystem {
public:
    // Removes editor-generated collider entities owned by a Tilemap.
    static void clear_composite_colliders(EntityManager& em, EntityID entity_id);
    // Rebuilds merged composite Box2D static colliders for a Tilemap entity
    static void build_composite_colliders(EntityManager& em, EntityID entity_id);
};

} // namespace Engine
