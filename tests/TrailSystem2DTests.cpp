#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "renderer/TrailSystem2D.hpp"
#include <cassert>

int main() {
    using namespace Engine;

    EntityManager entities;
    const EntityID projectile = entities.create_entity();
    TransformComponent transform;
    transform.world_position = {0.0f, 0.0f, 0.0f};
    entities.add_component<TransformComponent>(projectile, transform);
    TrailRenderer2DComponent trail;
    trail.lifetime = 0.25f;
    trail.min_vertex_distance = 3.0f;
    trail.max_points = 4;
    entities.add_component<TrailRenderer2DComponent>(projectile, trail);

    TrailSystem2D system;
    system.update(entities, 0.0f);
    assert(system.active_trail_count() == 1);
    assert(system.total_point_count() == 1);
    assert(system.point_count(projectile) == 1);

    entities.get_component<TransformComponent>(projectile).world_position.x = 1.0f;
    system.update(entities, 0.016f);
    assert(system.point_count(projectile) == 2);

    entities.get_component<TransformComponent>(projectile).world_position.x = 12.0f;
    system.update(entities, 0.016f);
    assert(system.point_count(projectile) == 3);

    for (int step = 0; step < 8; ++step) {
        entities.get_component<TransformComponent>(projectile).world_position.x += 4.0f;
        system.update(entities, 0.016f);
    }
    assert(system.point_count(projectile) <= 4);

    entities.destroy_entity(projectile);

    // Entity IDs are recycled immediately. A new projectile must begin a new
    // ribbon instead of connecting to the previous projectile's last point.
    const EntityID recycled = entities.create_entity();
    assert(recycled == projectile);
    TransformComponent recycled_transform;
    recycled_transform.world_position = {1000.0f, 700.0f, 0.0f};
    entities.add_component<TransformComponent>(recycled, recycled_transform);
    entities.add_component<TrailRenderer2DComponent>(recycled, trail);
    system.update(entities, 0.016f);
    assert(system.point_count(recycled) == 1);

    entities.destroy_entity(recycled);
    system.update(entities, 0.30f);
    assert(system.active_trail_count() == 0);
    return 0;
}
