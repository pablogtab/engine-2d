#include "GenericScene.hpp"
#include "../core/Log.hpp"
#include "../core/Input.hpp"
#include "../audio/AudioPresets.hpp"

namespace Engine {

GenericScene::GenericScene(std::string name, float width, float height)
    : Scene(std::move(name)), m_camera(width, height) {
    m_camera_controller.bind_camera(&m_camera);
    m_camera_controller.set_follow_speed(6.0f);
    m_camera_controller.enable_deadzone(false);
    m_camera_controller.enable_bounds(false);
}

void GenericScene::on_attach() {
    ENGINE_LOG_INFO("[SCENE] Attached generic scene: '{}'", get_name());
    Engine::PhysicsSystem::clear();
    Engine::PhysicsSystem::set_gravity(glm::vec2(0.0f, -980.0f));
}

void GenericScene::on_detach() {
    ENGINE_LOG_INFO("[SCENE] Detached generic scene: '{}'", get_name());
    Engine::PhysicsSystem::clear();
}

void GenericScene::on_update(float delta_time) {
    // --- 1. Player Gameplay Movement Controller (Active during Play simulation) ---
    EntityID player_id = 0;
    m_entity_manager.for_each<TagComponent>([&](EntityID id, const TagComponent& tag) {
        if (tag.tag == "Player_Knight" || tag.tag == "Player") {
            player_id = id;
        }
    });

    if (player_id != 0 && m_entity_manager.is_alive(player_id) && m_entity_manager.has_component<RigidBodyComponent>(player_id)) {
        auto& rb = m_entity_manager.get_component<RigidBodyComponent>(player_id);
        auto& tx = m_entity_manager.get_component<TransformComponent>(player_id);
        auto* sprite = m_entity_manager.has_component<SpriteComponent>(player_id) ? &m_entity_manager.get_component<SpriteComponent>(player_id) : nullptr;

        float move_x = 0.0f;
        if (Input::is_key_pressed(SDL_SCANCODE_A) || Input::is_key_pressed(SDL_SCANCODE_LEFT)) {
            move_x -= 1.0f;
            if (sprite) sprite->flip_x = true;
        }
        if (Input::is_key_pressed(SDL_SCANCODE_D) || Input::is_key_pressed(SDL_SCANCODE_RIGHT)) {
            move_x += 1.0f;
            if (sprite) sprite->flip_x = false;
        }

        float walk_speed = 280.0f;
        rb.velocity.x = move_x * walk_speed;
        PhysicsSystem::set_velocity(m_entity_manager, player_id, rb.velocity);

        // Jump
        if (Input::is_key_just_pressed(SDL_SCANCODE_SPACE) || Input::is_key_just_pressed(SDL_SCANCODE_W) || Input::is_key_just_pressed(SDL_SCANCODE_UP)) {
            if (std::abs(rb.velocity.y) < 15.0f) { // Grounded check
                rb.velocity.y = 520.0f;
                PhysicsSystem::set_velocity(m_entity_manager, player_id, rb.velocity);
                AudioPresets::play_jump(0.8f);
            }
        }

        // Camera follow
        glm::vec2 target_cam = glm::vec2(tx.position.x, tx.position.y + 60.0f);
        m_camera_controller.set_target(target_cam);

        // --- 2. Coin Collection & Trampoline Trigger Overlap Checks ---
        glm::vec2 p_pos = glm::vec2(tx.position.x, tx.position.y);
        std::vector<EntityID> coins_to_destroy;

        m_entity_manager.for_each<TagComponent, TransformComponent>([&](EntityID id, const TagComponent& tag, const TransformComponent& other_tx) {
            if (id == player_id) return;
            glm::vec2 o_pos = glm::vec2(other_tx.position.x, other_tx.position.y);
            float dist = glm::distance(p_pos, o_pos);

            // Coins
            if (tag.tag.rfind("Collectible_Coin", 0) == 0 || tag.tag == "Coin") {
                if (dist < 36.0f) {
                    coins_to_destroy.push_back(id);
                    AudioPresets::play_coin(0.9f);
                }
            }

            // Trampoline
            if (tag.tag == "Trampoline_Spring_Pad" || tag.tag == "Trampoline") {
                if (std::abs(p_pos.x - o_pos.x) < 40.0f && std::abs(p_pos.y - o_pos.y) < 30.0f && rb.velocity.y <= 0.0f) {
                    rb.velocity.y = 780.0f;
                    PhysicsSystem::set_velocity(m_entity_manager, player_id, rb.velocity);
                    AudioPresets::play_jump(1.0f);
                }
            }
        });

        for (EntityID c_id : coins_to_destroy) {
            m_entity_manager.destroy_entity(c_id);
        }
    }

    // --- 3. Physics & Transform Updates ---
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
