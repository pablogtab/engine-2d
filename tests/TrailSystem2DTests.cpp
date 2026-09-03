#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "renderer/TrailSystem2D.hpp"
#include <cassert>
#include <cmath>

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
    // The live head is promoted to the next sample at the distance threshold;
    // it does not create a duplicate point at the same position.
    assert(system.point_count(projectile) == 2);

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

    // Editor previews drive a disposable ECS copy. A representative figure-8
    // path must build a real ribbon without mutating its authored source.
    EntityManager authored;
    const EntityID authored_entity = authored.create_entity();
    TransformComponent authored_transform;
    authored_transform.world_position = {42.0f, -17.0f, 0.0f};
    authored.add_component<TransformComponent>(authored_entity, authored_transform);
    authored.add_component<TrailRenderer2DComponent>(authored_entity, trail);

    EntityManager preview;
    const EntityID preview_entity = preview.create_entity();
    preview.add_component<TransformComponent>(preview_entity, authored_transform);
    preview.add_component<TrailRenderer2DComponent>(preview_entity, trail);
    TrailSystem2D preview_system;
    for (int step = 0; step < 24; ++step) {
        const float phase = static_cast<float>(step) * 0.22f;
        auto& preview_transform = preview.get_component<TransformComponent>(preview_entity);
        preview_transform.world_position = {
            std::sin(phase) * 145.0f,
            std::sin(phase * 2.0f) * 68.0f,
            0.0f
        };
        preview_system.update(preview, 0.016f);
    }
    assert(preview_system.point_count(preview_entity) > 1);
    const auto& unchanged = authored.get_component<TransformComponent>(authored_entity);
    assert(unchanged.world_position.x == 42.0f);
    assert(unchanged.world_position.y == -17.0f);
    return 0;
}
