#include "ecs/EntityManager.hpp"
#include "ecs/components/CoreComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "scene/Camera2D.hpp"
#include "scene/CameraController2D.hpp"
#include <cassert>
#include <cmath>

namespace {

bool near(float lhs, float rhs, float tolerance = 0.01f) {
    return std::abs(lhs - rhs) <= tolerance;
}

} // namespace

int main() {
    using namespace Engine;

    EntityManager entities;
    const EntityID target = entities.create_entity();
    entities.add_component<TransformComponent>(target, TransformComponent{110.0f, 20.0f, 0.0f});

    const EntityID camera_entity = entities.create_entity();
    entities.add_component<TransformComponent>(camera_entity, TransformComponent{10.0f, 20.0f, 0.0f});
    CameraComponent authored_camera;
    authored_camera.is_primary = true;
    authored_camera.follow_target = target;
    authored_camera.follow_offset = {5.0f, 0.0f};
    authored_camera.follow_speed = 20.0f;
    entities.add_component<CameraComponent>(camera_entity, authored_camera);

    Camera2D runtime_camera(1920.0f, 1080.0f);
    CameraController2D controller(&runtime_camera);
    assert(controller.update_from_entities(entities, 0.0f, true));
    assert(near(runtime_camera.get_position().x, 10.0f));
    assert(near(runtime_camera.get_position().y, 20.0f));

    assert(controller.update_from_entities(entities, 1.0f));
    assert(near(runtime_camera.get_position().x, 115.0f));
    assert(near(runtime_camera.get_position().y, 20.0f));

    auto& camera = entities.get_component<CameraComponent>(camera_entity);
    camera.follow_target = 0;
    auto& transform = entities.get_component<TransformComponent>(camera_entity);
    transform.position = {30.0f, 40.0f, 0.0f};
    transform.world_position = transform.position;
    assert(controller.update_from_entities(entities, 0.016f));
    assert(near(runtime_camera.get_position().x, 30.0f));
    assert(near(runtime_camera.get_position().y, 40.0f));

    entities.destroy_entity(camera_entity);
    assert(!controller.update_from_entities(entities, 0.016f));
    assert(near(runtime_camera.get_position().x, 0.0f));
    assert(near(runtime_camera.get_position().y, 0.0f));
}
