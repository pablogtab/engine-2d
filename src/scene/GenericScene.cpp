#include "GenericScene.hpp"
#include "../core/Log.hpp"

namespace Engine {

GenericScene::GenericScene(std::string name, float width, float height)
    : Scene(std::move(name)), m_camera(width, height) {
    m_camera_controller.bind_camera(&m_camera);
    m_camera_controller.set_follow_speed(5.0f);
    m_camera_controller.enable_deadzone(false);
    m_camera_controller.enable_bounds(false);
}

void GenericScene::on_attach() {
    ENGINE_LOG_INFO("[SCENE] Attached generic scene: '{}'", get_name());
    Engine::PhysicsSystem::clear();
}

void GenericScene::on_detach() {
    ENGINE_LOG_INFO("[SCENE] Detached generic scene: '{}'", get_name());
    Engine::PhysicsSystem::clear();
    m_entity_manager.clear();
}

void GenericScene::on_update(float delta_time) {
    if (m_physics_enabled) {
        Engine::PhysicsSystem::update(m_entity_manager, m_event_dispatcher, delta_time);
    }
    Engine::TransformSystem::update(m_entity_manager);
    m_camera_controller.update(delta_time);
}

void GenericScene::on_render(Renderer2D& renderer) {
    renderer.begin_scene(m_camera);

    m_entity_manager.for_each<Engine::TransformComponent, Engine::SpriteComponent>(
        [&](Engine::EntityID entity, Engine::TransformComponent& transform, Engine::SpriteComponent& sprite) {
            (void)entity;
            if (!transform.world_visible || !sprite.visible || !sprite.texture) return;

            const SDL_FRect* src_ptr = sprite.use_src_rect ? &sprite.src_rect : nullptr;

            renderer.submit_sprite(
                sprite.texture,
                transform.world_position,
                sprite.size * transform.world_scale,
                transform.world_rotation,
                sprite.color,
                src_ptr,
                transform.world_pivot,
                sprite.flip_x,
                sprite.flip_y
            );
        }
    );

    renderer.end_scene();
}

void GenericScene::on_resize(int width, int height, float dpi_scale) {
    m_viewport_w = width;
    m_viewport_h = height;
    m_dpi_scale = dpi_scale;
    m_camera.set_viewport_size(static_cast<float>(width), static_cast<float>(height));
    m_camera.set_dpi_scale(dpi_scale);
}

} // namespace Engine
