#include "GenericScene.hpp"
#include "../core/Log.hpp"
#include "../core/Input.hpp"
#include "../audio/AudioPresets.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../ecs/CameraSystem.hpp"
#include "../ecs/RenderSystem.hpp"
#include "../renderer/SpriteAnimationSystem.hpp"
#include <algorithm>

namespace Engine {

GenericScene::GenericScene(std::string name, float width, float height)
    : Scene(std::move(name)), m_camera(width, height), m_editor_camera(width, height) {
    m_camera_controller.bind_camera(&m_camera);
    m_camera_controller.set_follow_speed(6.0f);
    m_camera_controller.enable_deadzone(false);
    m_camera_controller.enable_bounds(false);

    m_editor_camera.set_position(glm::vec2(0.0f, 0.0f));
    m_editor_camera.set_zoom(1.0f);

    // GenericScene remains a temporary gameplay helper, but collision geometry
    // must still come from the engine. Queue collectible destruction until
    // after Box2D finishes stepping; mutating bodies inside BeginContact is not
    // safe while the physics world is locked.
    m_event_dispatcher.subscribe<CollisionEvent>([this](const CollisionEvent& event) {
        if (!event.is_trigger) return;
        const auto is_player = [this](EntityID id) {
            if (!m_entity_manager.is_alive(id) || !m_entity_manager.has_component<TagComponent>(id)) return false;
            const std::string& tag = m_entity_manager.get_component<TagComponent>(id).tag;
            return tag == "Player_Knight" || tag == "Player";
        };
        const auto is_coin = [this](EntityID id) {
            if (!m_entity_manager.is_alive(id) || !m_entity_manager.has_component<TagComponent>(id)) return false;
            const std::string& tag = m_entity_manager.get_component<TagComponent>(id).tag;
            return tag.rfind("Collectible_Coin", 0) == 0 || tag == "Coin";
        };

        EntityID coin = 0;
        if (is_player(event.entity_a) && is_coin(event.entity_b)) coin = event.entity_b;
        if (is_player(event.entity_b) && is_coin(event.entity_a)) coin = event.entity_a;
        if (coin != 0 && std::find(m_pending_coin_collections.begin(), m_pending_coin_collections.end(), coin) == m_pending_coin_collections.end()) {
            m_pending_coin_collections.push_back(coin);
        }
    });
}

void GenericScene::on_attach() {
    ENGINE_LOG_INFO("[SCENE] Attached generic scene: '{}'", get_name());
    Engine::PhysicsSystem::clear();
    Engine::PhysicsSystem::set_gravity(glm::vec2(0.0f, -980.0f));

    TransformSystem::update(m_entity_manager);
    synchronize_game_camera(0.0f, true);
}

void GenericScene::on_detach() {
    ENGINE_LOG_INFO("[SCENE] Detached generic scene: '{}'", get_name());
    Engine::PhysicsSystem::clear();
}

void GenericScene::pan_editor_camera(const glm::vec2& delta_pixels) {
    float zoom = m_editor_camera.get_zoom();
    if (zoom <= 0.001f) zoom = 1.0f;
    glm::vec2 current_pos = m_editor_camera.get_position();
    m_editor_camera.set_position(current_pos + glm::vec2(-delta_pixels.x, delta_pixels.y) / zoom);
}

void GenericScene::zoom_editor_camera(float zoom_factor) {
    const float new_zoom = std::clamp(m_editor_camera.get_zoom() * zoom_factor, EDITOR_MIN_ZOOM, EDITOR_MAX_ZOOM);
    m_editor_camera.set_zoom(new_zoom);
}

void GenericScene::zoom_editor_camera_at(const glm::vec2& viewport_position, float zoom_factor) {
    const glm::vec2 world_before = m_editor_camera.screen_to_world(viewport_position);
    zoom_editor_camera(zoom_factor);
    const glm::vec2 world_after = m_editor_camera.screen_to_world(viewport_position);
    m_editor_camera.set_position(m_editor_camera.get_position() + world_before - world_after);
}

void GenericScene::focus_editor_camera(const glm::vec2& target_pos) {
    m_editor_camera.set_position(target_pos);
}

void GenericScene::frame_editor_camera(const glm::vec2& bounds_min, const glm::vec2& bounds_max, float padding) {
    const glm::vec2 center = (bounds_min + bounds_max) * 0.5f;
    glm::vec2 extent = glm::max(bounds_max - bounds_min, glm::vec2(1.0f));
    extent *= 1.0f + std::max(0.0f, padding) * 2.0f;

    const glm::vec2 viewport = m_editor_camera.get_viewport_size();
    const float zoom_x = viewport.x / extent.x;
    const float zoom_y = viewport.y / extent.y;
    m_editor_camera.set_position(center);
    m_editor_camera.set_zoom(std::clamp(std::min(zoom_x, zoom_y), EDITOR_MIN_ZOOM, EDITOR_MAX_ZOOM));
}

void GenericScene::reset_editor_camera() {
    m_editor_camera.set_position(glm::vec2(0.0f));
    m_editor_camera.set_zoom(1.0f);
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

        // Player Animation State Machine (Idle, Run, Jump, Fall)
        if (m_entity_manager.has_component<AnimationComponent>(player_id)) {
            bool is_grounded = std::abs(rb.velocity.y) < 15.0f;
            if (!is_grounded) {
                if (rb.velocity.y > 20.0f) {
                    SpriteAnimationSystem::play_clip(m_entity_manager, player_id, "Jump");
                } else if (rb.velocity.y < -20.0f) {
                    SpriteAnimationSystem::play_clip(m_entity_manager, player_id, "Fall");
                }
            } else {
                if (std::abs(rb.velocity.x) > 10.0f) {
                    SpriteAnimationSystem::play_clip(m_entity_manager, player_id, "Run");
                } else {
                    SpriteAnimationSystem::play_clip(m_entity_manager, player_id, "Idle");
                }
            }
        }

        // --- 2. Temporary trampoline helper ---
        glm::vec2 p_pos = glm::vec2(tx.world_position.x, tx.world_position.y);

        m_entity_manager.for_each<TagComponent, TransformComponent>([&](EntityID id, const TagComponent& tag, const TransformComponent& other_tx) {
            if (id == player_id) return;
            glm::vec2 o_pos = glm::vec2(other_tx.world_position.x, other_tx.world_position.y);

            // Trampoline
            if (tag.tag == "Trampoline_Spring_Pad" || tag.tag == "Trampoline") {
                if (std::abs(p_pos.x - o_pos.x) < 40.0f && std::abs(p_pos.y - o_pos.y) < 30.0f && rb.velocity.y <= 0.0f) {
                    rb.velocity.y = 780.0f;
                    PhysicsSystem::set_velocity(m_entity_manager, player_id, rb.velocity);
                    AudioPresets::play_jump(1.0f);
                }
            }
        });

    }

    // --- 3. Physics & Transform Updates ---
    if (m_physics_enabled) {
        Engine::PhysicsSystem::update(m_entity_manager, m_event_dispatcher, delta_time);
    }
    for (EntityID coin : m_pending_coin_collections) {
        if (!m_entity_manager.is_alive(coin)) continue;
        m_entity_manager.destroy_entity(coin);
        AudioPresets::play_coin(0.9f);
    }
    m_pending_coin_collections.clear();
    Engine::SpriteAnimationSystem::update(m_entity_manager, delta_time);
    Engine::TransformSystem::update(m_entity_manager);
    synchronize_game_camera(delta_time, false);
}

void GenericScene::synchronize_game_camera(float delta_time, bool reset_controller) {
    const EntityID primary = CameraSystem::find_primary(m_entity_manager);
    if (primary == 0) {
        m_active_camera_entity = 0;
        m_camera.set_position(glm::vec2(0.0f));
        m_camera.set_rotation(0.0f);
        m_camera.set_zoom(1.0f);
        return;
    }

    const bool camera_changed = primary != m_active_camera_entity;
    m_active_camera_entity = primary;
    if (!CameraSystem::apply_to_runtime(m_entity_manager, primary, m_camera, true)) return;

    const auto& camera = m_entity_manager.get_component<CameraComponent>(primary);
    const auto& camera_transform = m_entity_manager.get_component<TransformComponent>(primary);
    const glm::vec2 authored_position(camera_transform.world_position);
    if (reset_controller || camera_changed) m_camera_controller.reset_center(authored_position);

    m_camera_controller.set_follow_speed(camera.follow_speed);
    m_camera_controller.enable_bounds(camera.enable_bounds);
    if (camera.enable_bounds) {
        m_camera_controller.set_bounds(
            glm::vec2(camera.bounds.x, camera.bounds.y),
            glm::vec2(camera.bounds.z, camera.bounds.w)
        );
    }

    if (camera.follow_target != 0 &&
        m_entity_manager.is_alive(camera.follow_target) &&
        m_entity_manager.has_component<TransformComponent>(camera.follow_target)) {
        const auto& target = m_entity_manager.get_component<TransformComponent>(camera.follow_target);
        m_camera_controller.set_target(glm::vec2(target.world_position) + camera.follow_offset);
        m_camera_controller.update(delta_time);
    } else {
        m_camera.set_position(authored_position);
    }
}

void GenericScene::on_render(Renderer2D& renderer) {
    Camera2D& active_cam = m_is_editing ? m_editor_camera : m_camera;
    renderer.begin_scene(active_cam);

    // Centralized rendering for Sprites, Tilemaps, and Outlines
    RenderSystem::render(m_entity_manager, renderer);

    renderer.end_scene();
}

void GenericScene::on_viewport_resize(RenderViewKind view, const RenderViewConfiguration& configuration) {
    Camera2D& camera = view == RenderViewKind::Editor ? m_editor_camera : m_camera;
    camera.set_viewport_size(static_cast<float>(configuration.output_width), static_cast<float>(configuration.output_height));
    camera.set_reference_size(static_cast<float>(configuration.reference_width), static_cast<float>(configuration.reference_height));
}

} // namespace Engine
