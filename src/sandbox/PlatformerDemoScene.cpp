#include "PlatformerDemoScene.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../ecs/PhysicsSystem.hpp"
#include "../ecs/RenderSystem.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../renderer/Renderer2D.hpp"
#include <SDL3/SDL.h>
#include <cmath>

namespace Sandbox {

namespace Layer {
    constexpr uint16_t DEFAULT  = Engine::CollisionCategory::DEFAULT; // 0x0001
    constexpr uint16_t PLAYER   = 0x0002; // Layer 1: Player
    constexpr uint16_t GROUND   = 0x0004; // Layer 2: Platforms & Terrain
    constexpr uint16_t COIN     = 0x0008; // Layer 3: Collectible Coins
    constexpr uint16_t HAZARD   = 0x0010; // Layer 4: Hazards / Spikes
}

PlatformerDemoScene::PlatformerDemoScene(SDL_Renderer* renderer)
    : Engine::Scene("2D Platformer & Collision Layer Matrix"),
      m_sdl_renderer(renderer),
      m_camera(1280.0f, 720.0f) {}

void PlatformerDemoScene::on_attach() {
    ENGINE_LOG_INFO("[PLATFORMER DEMO] Initializing 2D Platformer Scene...");
    m_entity_manager.clear();
    Engine::PhysicsSystem::clear();
    m_score = 0;
    m_total_coins = 0;

    // Set Platformer Downward Gravity (Y = -800.0f pulls downward)
    Engine::PhysicsSystem::set_gravity(glm::vec2(0.0f, -800.0f));

    // Generate Procedural Textures
    m_player_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 64, 64, 40, 180, 255, 255)
    );

    m_block_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 64, 64, 120, 90, 60, 255)
    );

    m_coin_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_circle(m_sdl_renderer, 16, 255, 215, 0, 255)
    );

    m_launch_pad_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 60, 16, 220, 50, 255, 255)
    );

    m_elevator_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 100, 20, 0, 220, 255, 255)
    );

    // Procedural wheel texture with spokes generated locally for the sandbox demo
    {
        int radius = 24;
        int diameter = radius * 2;
        std::vector<uint32_t> pixels(diameter * diameter, 0);
        uint32_t spoke_color = (255 << 24) | (255 << 16) | (255 << 8) | 255;
        uint32_t mark_color  = (255 << 24) | (30 << 16)  | (30 << 8)  | 255;
        for (int y = 0; y < diameter; ++y) {
            for (int x = 0; x < diameter; ++x) {
                float dx = x - radius + 0.5f;
                float dy = y - radius + 0.5f;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq <= radius * radius) {
                    bool is_spoke = (std::abs(dx) <= 2.5f) || (std::abs(dy) <= 2.5f);
                    bool is_mark  = (dx > 4.0f && dy < -4.0f && dist_sq < (radius * 0.7f) * (radius * 0.7f));
                    if (is_spoke) {
                        pixels[y * diameter + x] = spoke_color;
                    } else if (is_mark) {
                        pixels[y * diameter + x] = mark_color;
                    } else {
                        pixels[y * diameter + x] = (255 << 24) | (140 << 16) | (0 << 8) | 255;
                    }
                }
            }
        }
        m_wheel_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, diameter, diameter, pixels.data());
    }

    m_circle_debug_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_circle(m_sdl_renderer, 32, 0, 255, 120, 180)
    );

    // Register Event Listener for Collectibles, Ground Contact, and Curve Force Launch Pads
    m_event_dispatcher.subscribe<Engine::CollisionEvent>(
        [this](const Engine::CollisionEvent& ev) {
            // Check Ground Contact for Player (Normal pointing UP from floor to feet)
            if (ev.entity_a == m_player_id || ev.entity_b == m_player_id) {
                if (!ev.is_trigger) {
                    glm::vec2 n = ev.contact_point;
                    // If normal points upward (+Y in Cartesian Y-up)
                    if (ev.entity_a == m_player_id && n.y > 0.4f) {
                        m_is_grounded = true;
                    } else if (ev.entity_b == m_player_id && n.y < -0.4f) {
                        m_is_grounded = true;
                    }
                }
            }

            // Launch Pad Premiere Cubic Bezier Force Launch Trigger (Single-shot cooldown)
            bool hit_pad = false;
            if (ev.entity_a == m_player_id && m_entity_manager.has_component<Engine::TagComponent>(ev.entity_b)) {
                if (m_entity_manager.get_component<Engine::TagComponent>(ev.entity_b).tag == "LaunchPad") {
                    hit_pad = true;
                }
            } else if (ev.entity_b == m_player_id && m_entity_manager.has_component<Engine::TagComponent>(ev.entity_a)) {
                if (m_entity_manager.get_component<Engine::TagComponent>(ev.entity_a).tag == "LaunchPad") {
                    hit_pad = true;
                }
            }

            if (hit_pad && m_launch_cooldown <= 0.0f) {
                m_launch_cooldown = 0.4f; // Prevent multi-frame trigger stacking
                auto& rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(m_player_id);

                // Explicitly reset downward Y-velocity on BOTH ECS and Box2D body before launching
                Engine::PhysicsSystem::set_velocity(m_entity_manager, m_player_id, glm::vec2(rb.velocity.x, 0.0f));

                // Apply fixed consistent spring bounce impulse (200.0 px/s height reach)
                Engine::PhysicsSystem::apply_impulse(m_entity_manager, m_player_id, glm::vec2(0.0f, 200.0f));
                ENGINE_LOG_INFO("[TRAMPOLINE BOUNCE] Fixed Consistent Spring Launch Applied! Velocity Y set to 200.0");
            }

            // Collectible Coin Sensor Listener
            Engine::EntityID coin_id = 0;
            bool hit_coin = false;

            if (ev.entity_a == m_player_id && m_entity_manager.has_component<Engine::TagComponent>(ev.entity_b)) {
                if (m_entity_manager.get_component<Engine::TagComponent>(ev.entity_b).tag == "Coin") {
                    coin_id = ev.entity_b;
                    hit_coin = true;
                }
            } else if (ev.entity_b == m_player_id && m_entity_manager.has_component<Engine::TagComponent>(ev.entity_a)) {
                if (m_entity_manager.get_component<Engine::TagComponent>(ev.entity_a).tag == "Coin") {
                    coin_id = ev.entity_a;
                    hit_coin = true;
                }
            }

            if (hit_coin && m_entity_manager.is_alive(coin_id)) {
                m_score++;
                ENGINE_LOG_INFO("[PLATFORMER DEMO] COIN COLLECTED! Score: {} / {}", m_score, m_total_coins);
                m_entity_manager.destroy_entity(coin_id);
            }
        }
    );

    setup_level();
}

void PlatformerDemoScene::on_detach() {
    ENGINE_LOG_INFO("[PLATFORMER DEMO] Detaching scene...");
    m_entity_manager.clear();
}

void PlatformerDemoScene::setup_level() {
    // 1. Create Main Ground Platform (Static RigidBody)
    create_platform({0.0f, -280.0f}, {1200.0f, 40.0f}, {80, 140, 60, 255});

    // 2. Create Floating Platforms
    create_platform({-350.0f, -120.0f}, {280.0f, 30.0f}, {140, 100, 70, 255});
    create_platform({0.0f, 20.0f}, {260.0f, 30.0f}, {140, 100, 70, 255});
    create_platform({380.0f, 150.0f}, {280.0f, 30.0f}, {140, 100, 70, 255});
    create_platform({-200.0f, 240.0f}, {220.0f, 30.0f}, {140, 100, 70, 255});

    // 3. Create Premiere Bezier Launch Pad Trigger on Floor
    create_launch_pad({150.0f, -252.0f});

    // 4. Spawn Collectible Coins (Triggers on Layer COIN)
    std::vector<glm::vec2> coin_positions = {
        {-420.0f, -60.0f}, {-280.0f, -60.0f},
        {-50.0f, 80.0f},   {50.0f, 80.0f},
        {320.0f, 210.0f},  {440.0f, 210.0f},
        {-200.0f, 300.0f}
    };

    for (const auto& pos : coin_positions) {
        create_coin(pos);
    }
    m_total_coins = static_cast<int>(coin_positions.size());

    // 5. Create Player Entity (Knight Jumper)
    m_player_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(m_player_id, Engine::TagComponent{"Player"});
    m_entity_manager.add_component<Engine::TransformComponent>(
        m_player_id, Engine::TransformComponent{glm::vec3(0.0f, -200.0f, 0.0f)}
    );
    m_entity_manager.add_component<Engine::SpriteComponent>(
        m_player_id, Engine::SpriteComponent{m_player_texture.get(), glm::vec2(44.0f, 56.0f), SDL_Color{50, 180, 255, 255}}
    );

    // Dynamic Physics RigidBody: 0.0f friction on sides prevents wall sticking!
    m_entity_manager.add_component<Engine::RigidBodyComponent>(
        m_player_id, Engine::RigidBodyComponent::Dynamic(1.0f, 0.0f, 0.0f, true)
    );

    // Player Collider: Category PLAYER, Mask GROUND | COIN | DEFAULT
    uint16_t player_mask = Layer::GROUND | Layer::COIN | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        m_player_id, Engine::ColliderComponent::Box({44.0f, 56.0f}, {0.0f, 0.0f}, false, Layer::PLAYER, player_mask)
    );

    // 6. Create Rolling Physical Wheel Entity (Free 2D Rotation: lock_rotation = false)
    Engine::EntityID wheel_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(wheel_id, Engine::TagComponent{"RollingWheel"});
    m_entity_manager.add_component<Engine::TransformComponent>(wheel_id, Engine::TransformComponent{glm::vec3(0.0f, 100.0f, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(wheel_id, Engine::SpriteComponent{m_wheel_texture.get(), glm::vec2(48.0f, 48.0f), SDL_Color{255, 255, 255, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(wheel_id, Engine::RigidBodyComponent::Dynamic(1.0f, 0.5f, 0.6f, false)); // lock_rotation = false!
    m_entity_manager.add_component<Engine::ColliderComponent>(wheel_id, Engine::ColliderComponent::Circle(24.0f, {0.0f, 0.0f}, false, Layer::DEFAULT, Layer::GROUND | Layer::PLAYER));

    // 7. Create Kinematic Elevator Moving Platform on the Left (X = -480.0)
    create_elevator({-480.0f, -220.0f}, {110.0f, 22.0f});
}

void PlatformerDemoScene::create_elevator(const glm::vec2& position, const glm::vec2& size) {
    m_elevator_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(m_elevator_id, Engine::TagComponent{"Elevator"});
    m_entity_manager.add_component<Engine::TransformComponent>(m_elevator_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(m_elevator_id, Engine::SpriteComponent{m_elevator_texture.get(), size, SDL_Color{0, 230, 255, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(m_elevator_id, Engine::RigidBodyComponent::Kinematic(0.9f)); // High surface grip friction

    // Platform Collider: Category GROUND, Mask PLAYER | DEFAULT
    uint16_t ground_mask = Layer::PLAYER | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        m_elevator_id, Engine::ColliderComponent::Box(size, {0.0f, 0.0f}, false, Layer::GROUND, ground_mask)
    );
}

void PlatformerDemoScene::create_platform(const glm::vec2& position, const glm::vec2& size, SDL_Color color) {
    Engine::EntityID platform_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(platform_id, Engine::TagComponent{"Platform"});
    m_entity_manager.add_component<Engine::TransformComponent>(platform_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(platform_id, Engine::SpriteComponent{m_block_texture.get(), size, color});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(platform_id, Engine::RigidBodyComponent::Static());

    // Platform Collider: Category GROUND, Mask PLAYER | DEFAULT
    uint16_t ground_mask = Layer::PLAYER | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        platform_id, Engine::ColliderComponent::Box(size, {0.0f, 0.0f}, false, Layer::GROUND, ground_mask)
    );
}

void PlatformerDemoScene::create_launch_pad(const glm::vec2& position) {
    Engine::EntityID pad_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(pad_id, Engine::TagComponent{"LaunchPad"});
    m_entity_manager.add_component<Engine::TransformComponent>(pad_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(pad_id, Engine::SpriteComponent{m_launch_pad_texture.get(), glm::vec2(60.0f, 16.0f), SDL_Color{220, 50, 255, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(pad_id, Engine::RigidBodyComponent::Static()); // Static sensor

    // LaunchPad Collider: Category COIN (Trigger), Mask PLAYER, is_trigger = true
    m_entity_manager.add_component<Engine::ColliderComponent>(
        pad_id, Engine::ColliderComponent::Box({60.0f, 16.0f}, {0.0f, 0.0f}, true, Layer::COIN, Layer::PLAYER)
    );
}

void PlatformerDemoScene::create_coin(const glm::vec2& position) {
    Engine::EntityID coin_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(coin_id, Engine::TagComponent{"Coin"});
    m_entity_manager.add_component<Engine::TransformComponent>(coin_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(coin_id, Engine::SpriteComponent{m_coin_texture.get(), glm::vec2(24.0f, 24.0f), SDL_Color{255, 215, 0, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(coin_id, Engine::RigidBodyComponent::Static()); // Static sensor

    // Coin Collider: Category COIN, Mask PLAYER, is_trigger = true
    m_entity_manager.add_component<Engine::ColliderComponent>(
        coin_id, Engine::ColliderComponent::Circle(12.0f, {0.0f, 0.0f}, true, Layer::COIN, Layer::PLAYER)
    );
}

void PlatformerDemoScene::on_update(float delta_time) {
    if (m_launch_cooldown > 0.0f) {
        m_launch_cooldown -= delta_time;
    }

    // 0. 'R' Key: Reset Scene Level & Player Position
    if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_R)) {
        ENGINE_LOG_INFO("[PLATFORMER DEMO] 'R' Key pressed! Resetting level...");
        on_attach();
        return;
    }

    // 1. Toggle Debug Overlay with 'G' key
    if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_G)) {
        m_show_debug_colliders = !m_show_debug_colliders;
        ENGINE_LOG_INFO("[PLATFORMER DEMO] Debug Wireframe Overlay: {}", m_show_debug_colliders ? "ON" : "OFF");
    }

    // 2. Player Controls & Jump Physics
    if (m_entity_manager.is_alive(m_player_id) && m_entity_manager.has_component<Engine::RigidBodyComponent>(m_player_id)) {
        auto& rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(m_player_id);

        float move_speed = 320.0f;
        float move_x = 0.0f;

        if (Engine::Input::is_key_pressed(SDL_SCANCODE_A) || Engine::Input::is_key_pressed(SDL_SCANCODE_LEFT)) {
            move_x -= move_speed;
        }
        if (Engine::Input::is_key_pressed(SDL_SCANCODE_D) || Engine::Input::is_key_pressed(SDL_SCANCODE_RIGHT)) {
            move_x += move_speed;
        }

        rb.velocity.x = move_x;

        // Grounded check: Physics contact manifold query OR velocity threshold (supports moving elevator & flat terrain)
        constexpr float VERTICAL_GROUND_THRESHOLD = 35.0f;
        bool is_physically_grounded = Engine::PhysicsSystem::is_grounded(m_entity_manager, m_player_id) ||
                                      (std::abs(rb.velocity.y) < VERTICAL_GROUND_THRESHOLD);

        if (is_physically_grounded) {
            m_coyote_timer = 0.12f; // 120ms coyote time window
        } else {
            m_coyote_timer -= delta_time;
        }

        // Jump Input Buffering
        if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_SPACE) ||
            Engine::Input::is_key_just_pressed(SDL_SCANCODE_W) ||
            Engine::Input::is_key_just_pressed(SDL_SCANCODE_UP)) {
            m_jump_buffer_timer = 0.12f; // 120ms jump buffer
        } else {
            m_jump_buffer_timer -= delta_time;
        }

        // Execute Jump if buffered and within coyote window
        if (m_jump_buffer_timer > 0.0f && m_coyote_timer > 0.0f) {
            rb.velocity.y = 520.0f;
            m_jump_buffer_timer = 0.0f;
            m_coyote_timer = 0.0f;
            ENGINE_LOG_INFO("[PLATFORMER DEMO] Jump Applied! Velocity Y set to 520.0");
        }
    }

    // Kinematic Elevator Movement: Clean waypointed ping-pong between bottom floor (Y=-230) and top platform (Y=180)
    if (m_entity_manager.is_alive(m_elevator_id) &&
        m_entity_manager.has_component<Engine::RigidBodyComponent>(m_elevator_id) &&
        m_entity_manager.has_component<Engine::TransformComponent>(m_elevator_id)) {
        auto& elevator_rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(m_elevator_id);
        const auto& elevator_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_elevator_id);

        constexpr float ELEVATOR_BOTTOM_Y = -230.0f;
        constexpr float ELEVATOR_TOP_Y    = 180.0f;
        constexpr float ELEVATOR_SPEED    = 130.0f;

        static float s_elevator_dir = 1.0f; // 1.0f = UP, -1.0f = DOWN
        if (elevator_tx.position.y >= ELEVATOR_TOP_Y) {
            s_elevator_dir = -1.0f;
        } else if (elevator_tx.position.y <= ELEVATOR_BOTTOM_Y) {
            s_elevator_dir = 1.0f;
        }
        elevator_rb.velocity.y = s_elevator_dir * ELEVATOR_SPEED;
    }

    // 3. Update Systems
    Engine::PhysicsSystem::update(m_entity_manager, m_event_dispatcher, delta_time);
    Engine::TransformSystem::update(m_entity_manager);

    // Live Position & Velocity Debug Logging (every 15 frames)
    static int s_pos_log_counter = 0;
    if (m_entity_manager.is_alive(m_player_id) && (++s_pos_log_counter % 15 == 0)) {
        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        const auto& player_rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(m_player_id);
        ENGINE_LOG_INFO("[PLAYER POS LOG] Pos=({:.1f}, {:.1f}) | Vel=({:.1f}, {:.1f})",
                        player_tx.position.x, player_tx.position.y, player_rb.velocity.x, player_rb.velocity.y);
    }

    // 4. Smooth Camera Follow
    if (m_entity_manager.is_alive(m_player_id)) {
        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        glm::vec2 current_cam = m_camera.get_position();
        glm::vec2 target_cam = glm::vec2(player_tx.position.x, player_tx.position.y + 60.0f);
        m_camera.set_position(current_cam + (target_cam - current_cam) * 6.0f * delta_time);
    }
}

void PlatformerDemoScene::on_render(Engine::Renderer2D& renderer) {
    renderer.begin_scene(m_camera);
    Engine::RenderSystem::render(m_entity_manager, renderer);

    if (m_show_debug_colliders) {
        Engine::RenderSystem::render_debug_colliders(m_entity_manager, renderer, m_circle_debug_texture.get());
    }

    renderer.end_scene();
}

} // namespace Sandbox
