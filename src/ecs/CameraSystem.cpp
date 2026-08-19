#include "CameraSystem.hpp"

#include "components/CoreComponents.hpp"
#include "components/RenderingComponents.hpp"
#include "../scene/Camera2D.hpp"

namespace Engine {

EntityID CameraSystem::find_primary(EntityManager& entity_manager) {
    EntityID first_camera = 0;
    EntityID primary_camera = 0;
    entity_manager.for_each<CameraComponent, TransformComponent>(
        [&](EntityID id, const CameraComponent& camera, const TransformComponent&) {
            if (first_camera == 0) first_camera = id;
            if (primary_camera == 0 && camera.is_primary) primary_camera = id;
        }
    );
    return primary_camera != 0 ? primary_camera : first_camera;
}

bool CameraSystem::has_camera(EntityManager& entity_manager) {
    return find_primary(entity_manager) != 0;
}

void CameraSystem::make_primary(EntityManager& entity_manager, EntityID entity) {
    if (!entity_manager.is_alive(entity) || !entity_manager.has_component<CameraComponent>(entity)) return;
    entity_manager.for_each<CameraComponent>([entity](EntityID id, CameraComponent& camera) {
        camera.is_primary = id == entity;
    });
}

bool CameraSystem::apply_to_runtime(
    EntityManager& entity_manager,
    EntityID entity,
    Camera2D& runtime_camera,
    bool apply_position
) {
    if (!entity_manager.is_alive(entity) ||
        !entity_manager.has_component<CameraComponent>(entity) ||
        !entity_manager.has_component<TransformComponent>(entity)) {
        return false;
    }

    const auto& camera = entity_manager.get_component<CameraComponent>(entity);
    const auto& transform = entity_manager.get_component<TransformComponent>(entity);
    if (apply_position) {
        runtime_camera.set_position(glm::vec2(transform.world_position));
    }
    runtime_camera.set_rotation(transform.world_rotation);
    runtime_camera.set_zoom(camera.zoom);
    runtime_camera.set_projection_mode(camera.projection);
    return true;
}

} // namespace Engine
