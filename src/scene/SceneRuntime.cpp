#include "SceneRuntime.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../renderer/SpriteAnimationSystem.hpp"

namespace Engine {

SceneRuntime::SceneRuntime() : m_game_camera(1920.0f, 1080.0f) {
    m_camera_controller.bind_camera(&m_game_camera);
    m_camera_controller.set_follow_speed(6.0f);
    m_camera_controller.enable_deadzone(false);
    m_camera_controller.enable_bounds(false);
}

void SceneRuntime::attach(Scene& scene) {
    PhysicsSystem::clear();
    PhysicsSystem::set_gravity(scene.settings().gravity);
    TransformSystem::update(scene.get_entity_manager());
    m_ui_input.reset();
    synchronize_game_camera(scene, 0.0f, true);
    ENGINE_LOG_INFO("[SCENE RUNTIME] Attached scene '{}'", scene.get_name());
}

void SceneRuntime::detach() {
    PhysicsSystem::clear();
    m_ui_input.reset();
    m_active_camera_entity = 0;
}

void SceneRuntime::configure_game_view(const RenderViewConfiguration& configuration) {
    m_ui_output_size = {
        static_cast<float>(configuration.output_width),
        static_cast<float>(configuration.output_height)
    };
    m_ui_safe_area = configuration.safe_area;
    m_game_camera.set_viewport_size(
        static_cast<float>(configuration.output_width),
        static_cast<float>(configuration.output_height)
    );
    m_game_camera.set_reference_size(
        static_cast<float>(configuration.reference_width),
        static_cast<float>(configuration.reference_height)
    );
}

void SceneRuntime::update(Scene& scene, float delta_time) {
    const bool ui_had_focus = m_ui_input.focused_entity() != 0;
    const bool navigate_previous = Input::is_key_just_pressed(SDL_SCANCODE_UP);
    const bool navigate_next = Input::is_key_just_pressed(SDL_SCANCODE_DOWN);
    const bool activate_pressed = ui_had_focus &&
        (Input::is_key_just_pressed(SDL_SCANCODE_RETURN) || Input::is_key_just_pressed(SDL_SCANCODE_SPACE));
    const bool activate_released = ui_had_focus &&
        (Input::is_key_just_released(SDL_SCANCODE_RETURN) || Input::is_key_just_released(SDL_SCANCODE_SPACE));
    static_cast<void>(m_ui_input.update(
        scene.get_entity_manager(),
        m_ui_output_size,
        m_ui_safe_area,
        {
            .pointer_position = Input::get_mouse_position(),
            .pointer_down = Input::is_mouse_pressed(MouseButton::LEFT),
            .pointer_pressed = Input::is_mouse_just_pressed(MouseButton::LEFT),
            .pointer_released = Input::is_mouse_just_released(MouseButton::LEFT),
            .focus_next = navigate_next,
            .focus_previous = navigate_previous,
            .activate_down = ui_had_focus &&
                (Input::is_key_pressed(SDL_SCANCODE_RETURN) || Input::is_key_pressed(SDL_SCANCODE_SPACE)),
            .activate_pressed = activate_pressed,
            .activate_released = activate_released
        },
        &scene.get_event_dispatcher()
    ));
    if (scene.settings().physics_enabled) {
        PhysicsSystem::update(scene.get_entity_manager(), scene.get_event_dispatcher(), delta_time);
    }
    SpriteAnimationSystem::update(scene.get_entity_manager(), delta_time);
    TransformSystem::update(scene.get_entity_manager());
    synchronize_game_camera(scene, delta_time, false);
}

void SceneRuntime::synchronize_game_camera(Scene& scene, float delta_time, bool reset_controller) {
    EntityManager& entities = scene.get_entity_manager();
    const EntityID primary = CameraSystem::find_primary(entities);
    if (primary == 0) {
        m_active_camera_entity = 0;
        m_game_camera.set_position(glm::vec2(0.0f));
        m_game_camera.set_rotation(0.0f);
        m_game_camera.set_zoom(1.0f);
        return;
    }

    const bool camera_changed = primary != m_active_camera_entity;
    m_active_camera_entity = primary;
    if (!CameraSystem::apply_to_runtime(entities, primary, m_game_camera, true)) return;

    const auto& camera = entities.get_component<CameraComponent>(primary);
    const auto& transform = entities.get_component<TransformComponent>(primary);
    const glm::vec2 authored_position(transform.world_position);
    if (reset_controller || camera_changed) m_camera_controller.reset_center(authored_position);

    m_camera_controller.set_follow_speed(camera.follow_speed);
    m_camera_controller.enable_bounds(camera.enable_bounds);
    if (camera.enable_bounds) {
        m_camera_controller.set_bounds(
            glm::vec2(camera.bounds.x, camera.bounds.y),
            glm::vec2(camera.bounds.z, camera.bounds.w)
        );
    }
    if (camera.follow_target != 0 && entities.is_alive(camera.follow_target) &&
        entities.has_component<TransformComponent>(camera.follow_target)) {
        const auto& target = entities.get_component<TransformComponent>(camera.follow_target);
        m_camera_controller.set_target(glm::vec2(target.world_position) + camera.follow_offset);
        m_camera_controller.update(delta_time);
    } else {
        m_game_camera.set_position(authored_position);
    }
}

} // namespace Engine
