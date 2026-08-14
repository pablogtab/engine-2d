#include "PlatformerDemoScene.hpp"
#include "../core/Input.hpp"
#include "../core/Log.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../ecs/RenderSystem.hpp"
#include "../ecs/TransformSystem.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../particles/ParticlePresets.hpp"
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
    m_particle_system.clear();
    m_particle_system.init_default_textures(m_sdl_renderer);
    m_score = 0;
    m_total_coins = 0;

    // Set Platformer Downward Gravity (Y = -800.0f pulls downward)
    Engine::PhysicsSystem::set_gravity(glm::vec2(0.0f, -800.0f));

    // Initialize Post-Processing Shader Pipeline & All Passes
    m_post_process_pipeline.init(m_sdl_renderer, 1280, 720);
    m_post_process_pipeline.clear_passes();
    m_post_process_pipeline.add_pass(std::make_shared<Engine::VignettePass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::ChromaticAberrationPass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::BloomPass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::GaussianBlurPass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::ColorGradingPass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::FilmGrainPass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::ScanlinesCRTPass>());
    m_post_process_pipeline.add_pass(std::make_shared<Engine::ScreenFlashPass>());

    m_base_profile = Engine::PostProcessProfile::PresetDefault();
    m_base_profile.apply_to_pipeline(m_post_process_pipeline);

    // Setup 2D Spatial Post-Process Atmospheric Volumes
    m_volume_manager.clear();
    // 1. Far-Left Underground Crystal Cavern (Gentle aquatic cave tone with 150px feathering)
    m_volume_manager.add_volume(Engine::PostProcessVolume(
        "Crystal Cavern",
        glm::vec2(-620.0f, -160.0f),
        glm::vec2(280.0f, 260.0f),
        Engine::PostProcessProfile::PresetUnderwater(),
        150.0f
    ));
    // 2. High-Altitude Celestial Sanctuary (Upper floating platforms with 160px feathering)
    m_volume_manager.add_volume(Engine::PostProcessVolume(
        "Celestial Sanctuary",
        glm::vec2(0.0f, 320.0f),
        glm::vec2(640.0f, 220.0f),
        Engine::PostProcessProfile::PresetDreamyBloom(),
        160.0f
    ));

    // Wider camera zoom to frame the whole level comfortably
    m_camera.set_zoom(0.76f);
    m_camera.set_position(glm::vec2(0.0f, -20.0f));

    // Generate Procedural Textures
    m_player_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 64, 64, 40, 180, 255, 255)
    );

    // 1. Procedural Atmospheric Sky Backdrop Gradient
    {
        Engine::ProceduralTextureDesc sky_desc;
        sky_desc.type = Engine::ProceduralType::LinearGradient;
        sky_desc.width = 256;
        sky_desc.height = 256;
        sky_desc.color_a = SDL_Color{15, 23, 42, 255};   // Deep midnight slate (#0f172a)
        sky_desc.color_b = SDL_Color{30, 41, 59, 255};   // Horizon navy (#1e293b)
        m_sky_bg_texture = std::unique_ptr<Engine::Texture>(
            Engine::Texture::create_procedural(m_sdl_renderer, sky_desc)
        );
    }

    // 2. High-Contrast Tileable Earth & Grass Block (64x64)
    {
        int w = 64, h = 64;
        std::vector<uint32_t> pixels(w * h, 0);

        for (int y = 0; y < h; ++y) {
            float norm_y = static_cast<float>(y) / static_cast<float>(h);
            for (int x = 0; x < w; ++x) {
                float norm_x = static_cast<float>(x) / static_cast<float>(w);
                float n = std::sin(norm_x * 16.0f) * 0.15f + std::cos(norm_y * 18.0f) * 0.15f;

                if (y < 8) {
                    // Top vibrant grass trim with jagged blades
                    bool is_blade = (y >= 5) && ((x % 6 == 0) || (x % 6 == 1));
                    if (y < 4 || is_blade) {
                        pixels[y * w + x] = (90 << 24) | (185 << 16) | (45 << 8) | 255;  // #5ab92d Lush emerald grass
                    } else {
                        pixels[y * w + x] = (65 << 24) | (140 << 16) | (35 << 8) | 255;  // #418c23 Deep grass root
                    }
                } else if (y < 11) {
                    // Root transition band
                    pixels[y * w + x] = (95 << 24) | (60 << 16) | (38 << 8) | 255;
                } else if (y >= h - 2) {
                    // Bottom dark occlusion rim
                    pixels[y * w + x] = (38 << 24) | (22 << 16) | (14 << 8) | 255;
                } else {
                    // Rich fertile soil with organic noise
                    uint8_t r = static_cast<uint8_t>(std::clamp(115.0f + n * 28.0f, 85.0f, 145.0f));
                    uint8_t g = static_cast<uint8_t>(std::clamp(75.0f + n * 18.0f, 55.0f, 95.0f));
                    uint8_t b = static_cast<uint8_t>(std::clamp(48.0f + n * 14.0f, 32.0f, 65.0f));
                    pixels[y * w + x] = (r << 24) | (g << 16) | (b << 8) | 255;
                }
            }
        }
        m_block_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, w, h, pixels.data());
    }

    // 3. Tileable Polished Wooden Platform (64x24)
    {
        int w = 64, h = 24;
        std::vector<uint32_t> pixels(w * h, 0);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (y <= 1) {
                    pixels[y * w + x] = (225 << 24) | (165 << 16) | (95 << 8) | 255;  // Top highlight bevel
                } else if (y >= h - 2 || x == 0 || x == w - 1) {
                    pixels[y * w + x] = (95 << 24) | (52 << 16) | (26 << 8) | 255;   // Dark plank border
                } else {
                    float grain = std::sin(static_cast<float>(y) * 1.6f + static_cast<float>(x) * 0.12f) * 12.0f;
                    uint8_t r = static_cast<uint8_t>(std::clamp(180.0f + grain, 145.0f, 215.0f));
                    uint8_t g = static_cast<uint8_t>(std::clamp(120.0f + grain * 0.7f, 95.0f, 150.0f));
                    uint8_t b = static_cast<uint8_t>(std::clamp(68.0f + grain * 0.4f, 52.0f, 88.0f));
                    pixels[y * w + x] = (r << 24) | (g << 16) | (b << 8) | 255;
                }
            }
        }
        m_wood_platform_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, w, h, pixels.data());
    }

    m_coin_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_circle(m_sdl_renderer, 16, 255, 215, 0, 255)
    );

    m_launch_pad_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 60, 16, 220, 50, 255, 255)
    );

    m_elevator_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_solid(m_sdl_renderer, 100, 20, 0, 220, 255, 255)
    );

    // Procedural right-triangle slope texture (128x128 with grass top trim and anti-aliasing)
    {
        int w = 128, h = 128;
        std::vector<uint32_t> pixels(w * h, 0);
        uint32_t body_color  = (140 << 24) | (100 << 16) | (70 << 8)  | 255; // Wooden earth
        uint32_t grass_color = (80 << 24)  | (140 << 16) | (60 << 8)  | 255; // Grass green matching ground floor

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                // Diagonal hypotenuse from (0, h) [bottom-left] to (w, 0) [top-right]
                float target_y = (1.0f - static_cast<float>(x) / static_cast<float>(w)) * static_cast<float>(h);
                float dist_below = static_cast<float>(y) - target_y;

                if (dist_below >= 0.0f) {
                    if (dist_below < 6.0f) {
                        pixels[y * w + x] = grass_color; // Grass trim along the slope
                    } else {
                        pixels[y * w + x] = body_color;
                    }
                } else if (dist_below > -1.0f) {
                    float alpha_factor = 1.0f + dist_below;
                    uint8_t a = static_cast<uint8_t>(255.0f * alpha_factor);
                    pixels[y * w + x] = (80 << 24) | (140 << 16) | (60 << 8) | a;
                }
            }
        }
        m_slope_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, w, h, pixels.data());
    }

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

    // Procedural metallic wrecking ball texture (56x56)
    {
        int radius = 28;
        int diameter = radius * 2;
        std::vector<uint32_t> pixels(diameter * diameter, 0);
        uint32_t metal_base = (70 << 24) | (75 << 16) | (85 << 8) | 255;
        uint32_t highlight  = (160 << 24) | (170 << 16) | (190 << 8) | 255;
        uint32_t rim_dark   = (40 << 24) | (42 << 16) | (48 << 8) | 255;

        for (int y = 0; y < diameter; ++y) {
            for (int x = 0; x < diameter; ++x) {
                float dx = x - radius + 0.5f;
                float dy = y - radius + 0.5f;
                float dist_sq = dx * dx + dy * dy;
                if (dist_sq <= radius * radius) {
                    float dist = std::sqrt(dist_sq);
                    float hdx = dx + 7.0f;
                    float hdy = dy + 7.0f;
                    float h_dist = std::sqrt(hdx * hdx + hdy * hdy);
                    if (h_dist < 6.0f) {
                        pixels[y * diameter + x] = highlight;
                    } else if (dist > radius - 2.5f) {
                        pixels[y * diameter + x] = rim_dark;
                    } else {
                        pixels[y * diameter + x] = metal_base;
                    }
                }
            }
        }
        m_wrecking_ball_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, diameter, diameter, pixels.data());
    }

    m_circle_debug_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::create_circle(m_sdl_renderer, 32, 0, 255, 120, 180)
    );

    // 13. Procedural Campfire Firewood Logs (Charred rustic wood with glowing amber base)
    {
        constexpr int width = 52;
        constexpr int height = 26;
        std::vector<uint32_t> pixels(width * height, 0);

        uint32_t bark_dark   = (255 << 24) | (35 << 16) | (20 << 8) | 12;
        uint32_t bark_mid    = (255 << 24) | (65 << 16) | (38 << 8) | 22;
        uint32_t wood_core   = (255 << 24) | (95 << 16) | (58 << 8) | 32;
        uint32_t ember_glow  = (255 << 24) | (255 << 16) | (110 << 8) | 20;
        uint32_t ash_grey    = (255 << 24) | (70 << 16) | (68 << 8) | 72;

        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float nx = (static_cast<float>(x) - width * 0.5f) / (width * 0.5f);
                float ny = (static_cast<float>(y) - height * 0.3f) / (height * 0.7f);

                // Diagonal log 1 (Left to Right)
                float d1 = std::abs(ny - nx * 0.45f);
                // Diagonal log 2 (Right to Left)
                float d2 = std::abs(ny + nx * 0.45f);
                // Bottom foundation log
                float d3 = std::abs(ny + 0.2f);

                if (d1 < 0.35f || d2 < 0.35f || (d3 < 0.25f && std::abs(nx) < 0.85f)) {
                    if (y < 8 && std::abs(nx) < 0.4f) {
                        pixels[y * width + x] = ember_glow; // Hot burning embers at center
                    } else if (y < 12 && std::abs(nx) < 0.5f) {
                        pixels[y * width + x] = ash_grey;   // Ash ring
                    } else if ((x + y) % 6 == 0) {
                        pixels[y * width + x] = bark_dark;  // Bark grooves
                    } else {
                        pixels[y * width + x] = (d1 < 0.18f || d2 < 0.18f) ? wood_core : bark_mid;
                    }
                }
            }
        }
        m_campfire_logs_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, width, height, pixels.data());
    }

    // 14. Procedural 2D Dynamic Point Light Mask (Soft Gaussian radial falloff)
    {
        constexpr int size = 128;
        std::vector<uint32_t> pixels(size * size, 0);

        for (int y = 0; y < size; ++y) {
            float ny = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
            for (int x = 0; x < size; ++x) {
                float nx = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
                float dist = std::sqrt(nx * nx + ny * ny);

                if (dist < 1.0f) {
                    float falloff = (1.0f - dist) * (1.0f - dist); // Quadratic soft falloff
                    uint8_t a = static_cast<uint8_t>(falloff * 255.0f);
                    pixels[y * size + x] = (255 << 24) | (255 << 16) | (255 << 8) | a;
                }
            }
        }
        m_campfire_light_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, size, size, pixels.data());
    }

    // 15. Procedural Incandescent Flame Core (Teardrop shape, white/gold radiant gradient)
    {
        constexpr int width = 32;
        constexpr int height = 48;
        std::vector<uint32_t> pixels(width * height, 0);

        for (int y = 0; y < height; ++y) {
            float ny = static_cast<float>(y) / static_cast<float>(height); // 0 at base, 1 at tip
            float taper = 1.0f - ny * 0.85f; // Tapers toward flame tip
            for (int x = 0; x < width; ++x) {
                float nx = (static_cast<float>(x) - width * 0.5f) / (width * 0.5f * std::max(0.12f, taper));
                float dist_sq = nx * nx + (ny - 0.25f) * (ny - 0.25f) * 1.6f;

                if (dist_sq < 1.0f) {
                    float falloff = std::pow(1.0f - std::sqrt(dist_sq), 1.2f);
                    uint8_t a = static_cast<uint8_t>(falloff * 255.0f);
                    // Pure white with flame alpha falloff (so tinting turns it radiant gold/orange)
                    pixels[y * width + x] = (255 << 24) | (255 << 16) | (255 << 8) | a;
                }
            }
        }
        m_flame_core_texture = std::make_unique<Engine::Texture>(m_sdl_renderer, width, height, pixels.data());
    }

    // 16. Animated Knight Character Spritesheet & Animation Clips
    m_knight_spritesheet_texture = std::unique_ptr<Engine::Texture>(
        Engine::Texture::load_from_file(m_sdl_renderer, "assets/textures/knight_sheet.png")
    );
    if (m_knight_spritesheet_texture && m_knight_spritesheet_texture->get_width() > 0) {
        m_knight_spritesheet = std::make_unique<Engine::SpriteSheet>(m_knight_spritesheet_texture.get(), 8, 3);

        // Row 0: Idle (8 frames)
        m_anim_idle = Engine::Animation2D(m_knight_spritesheet.get(), 0, 7, 8.0f, true);

        // Row 1: Run (6 frames: frames 8..13)
        m_anim_run = Engine::Animation2D(m_knight_spritesheet.get(), 8, 13, 11.0f, true);

        // Row 2: Jump (frames 16..19) & Fall (frames 20..23)
        m_anim_jump = Engine::Animation2D(m_knight_spritesheet.get(), 16, 19, 8.0f, false);
        m_anim_fall = Engine::Animation2D(m_knight_spritesheet.get(), 20, 23, 8.0f, true);

        m_current_player_anim = &m_anim_idle;
        m_current_player_anim->play();
        ENGINE_LOG_INFO("[PLATFORMER DEMO] Animated Knight Spritesheet loaded: {}x{}", m_knight_spritesheet_texture->get_width(), m_knight_spritesheet_texture->get_height());
    }

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
                Engine::PostProcessTweens::trigger_flash(m_post_process_pipeline, SDL_Color{255, 235, 120, 80}, 0.14f);
                if (m_entity_manager.has_component<Engine::TransformComponent>(coin_id)) {
                    const auto& coin_tx = m_entity_manager.get_component<Engine::TransformComponent>(coin_id);
                    Engine::Presets::sparkle(m_particle_system, glm::vec2(coin_tx.position.x, coin_tx.position.y), 18);
                }
                m_entity_manager.destroy_entity(coin_id);
            }
        }
    );

    setup_level();
}

void PlatformerDemoScene::on_detach() {
    ENGINE_LOG_INFO("[PLATFORMER DEMO] Detaching scene...");
    m_entity_manager.clear();
    m_post_process_pipeline.destroy_targets();
}

void PlatformerDemoScene::on_resize(int width, int height) {
    m_camera.set_viewport_size(static_cast<float>(width), static_cast<float>(height));
    m_post_process_pipeline.resize(width, height);
}

void PlatformerDemoScene::setup_level() {
    // 0. Atmospheric Sky Backdrop (Far Z Layer = -10.0f for gentle parallax)
    {
        Engine::EntityID bg_id = m_entity_manager.create_entity();
        m_entity_manager.add_component<Engine::TagComponent>(bg_id, Engine::TagComponent{"SkyBackdrop"});
        m_entity_manager.add_component<Engine::TransformComponent>(
            bg_id, Engine::TransformComponent{glm::vec3(0.0f, 0.0f, -10.0f)}
        );
        m_entity_manager.add_component<Engine::SpriteComponent>(
            bg_id, Engine::SpriteComponent{m_sky_bg_texture.get(), glm::vec2(3200.0f, 1800.0f), SDL_Color{255, 255, 255, 255}}
        );
    }

    // 1. Create Main Ground Platform (Modularly tiled 1400px wide terrain)
    create_platform({0.0f, -280.0f}, {1400.0f, 40.0f}, {255, 255, 255, 255});

    // 2. Left Zone: Kinematic Elevator + Top Observation Deck + Stepping Platforms
    create_elevator({-500.0f, -220.0f}, {110.0f, 22.0f});
    create_platform({-500.0f, 180.0f}, {140.0f, 24.0f}, {255, 255, 255, 255}); // Top deck
    create_one_way_platform({-320.0f, -60.0f}, {180.0f, 22.0f}, {255, 255, 255, 255});
    create_one_way_platform({-320.0f, 80.0f}, {180.0f, 22.0f}, {255, 255, 255, 255});

    // 3. Center Zone: Solid Ceiling Arch + Swinging Wrecking Ball + Central Launch Pad
    create_platform({0.0f, 220.0f}, {180.0f, 24.0f}, {255, 255, 255, 255});
    create_wrecking_ball({0.0f, 208.0f}, {55.0f, 80.0f}, 26.0f);
    create_launch_pad({0.0f, -252.0f}); // Trampoline right under the open swing zone!

    // 4. Right Zone: Slope Ramp + Raised Deck + Rolling Wheel + High Floating Deck
    create_slope({280.0f, -185.0f}, {180.0f, 150.0f}, true);
    create_platform({460.0f, -125.0f}, {180.0f, 30.0f}, {255, 255, 255, 255});
    create_one_way_platform({280.0f, 70.0f}, {180.0f, 22.0f}, {255, 255, 255, 255});

    // 5. Spawn Collectible Coins in exciting reward spots
    std::vector<glm::vec2> coin_positions = {
        {-500.0f, 230.0f},                  // Above Elevator Top Deck
        {-320.0f, 130.0f},                  // Above Mid-Left Platform
        {-45.0f, -70.0f}, {0.0f, -20.0f}, {45.0f, -70.0f}, // Parabolic Arc above Launch Pad!
        {280.0f, 120.0f},                   // Above High Right Platform
        {460.0f, -60.0f}                    // Above Raised Wooden Deck
    };

    for (const auto& pos : coin_positions) {
        create_coin(pos);
    }
    m_total_coins = static_cast<int>(coin_positions.size());

    // 6. Create Player Entity (Knight Jumper)
    m_player_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(m_player_id, Engine::TagComponent{"Player"});
    m_entity_manager.add_component<Engine::TransformComponent>(
        m_player_id, Engine::TransformComponent{glm::vec3(-150.0f, -200.0f, 0.0f)}
    );
    
    Engine::SpriteComponent player_sprite{
        m_knight_spritesheet_texture ? m_knight_spritesheet_texture.get() : m_player_texture.get(),
        glm::vec2(56.0f, 56.0f),
        SDL_Color{255, 255, 255, 255}
    };
    if (m_knight_spritesheet) {
        player_sprite.use_src_rect = true;
        player_sprite.src_rect = m_anim_idle.get_current_frame_rect();
    }
    m_entity_manager.add_component<Engine::SpriteComponent>(m_player_id, player_sprite);

    // Dynamic Physics RigidBody: 0.0f friction on sides prevents wall sticking!
    m_entity_manager.add_component<Engine::RigidBodyComponent>(
        m_player_id, Engine::RigidBodyComponent::Dynamic(1.0f, 0.0f, 0.0f, true)
    );

    // Player Collider: Category PLAYER, Mask GROUND | COIN | DEFAULT
    uint16_t player_mask = Layer::GROUND | Layer::COIN | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        m_player_id, Engine::ColliderComponent::Box({42.0f, 52.0f}, {0.0f, 0.0f}, false, Layer::PLAYER, player_mask)
    );

    // 7. Create Rolling Physical Wheel Entity on Raised Platform (Rolls down slope!)
    Engine::EntityID wheel_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(wheel_id, Engine::TagComponent{"RollingWheel"});
    m_entity_manager.add_component<Engine::TransformComponent>(wheel_id, Engine::TransformComponent{glm::vec3(450.0f, -80.0f, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(wheel_id, Engine::SpriteComponent{m_wheel_texture.get(), glm::vec2(48.0f, 48.0f), SDL_Color{255, 255, 255, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(wheel_id, Engine::RigidBodyComponent::Dynamic(1.0f, 0.4f, 0.6f, false)); // lock_rotation = false!
    m_entity_manager.add_component<Engine::ColliderComponent>(wheel_id, Engine::ColliderComponent::Circle(24.0f, {0.0f, 0.0f}, false, Layer::DEFAULT, Layer::GROUND | Layer::PLAYER));
}

void PlatformerDemoScene::create_wrecking_ball(const glm::vec2& anchor_pos, const glm::vec2& ball_pos, float radius) {
    // 1. Static Anchor Pin
    Engine::EntityID anchor_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(anchor_id, Engine::TagComponent{"PendulumAnchor"});
    m_entity_manager.add_component<Engine::TransformComponent>(anchor_id, Engine::TransformComponent{glm::vec3(anchor_pos, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(anchor_id, Engine::SpriteComponent{m_block_texture.get(), glm::vec2(16.0f, 16.0f), SDL_Color{80, 80, 80, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(anchor_id, Engine::RigidBodyComponent::Static());
    m_entity_manager.add_component<Engine::ColliderComponent>(anchor_id, Engine::ColliderComponent::Box({16.0f, 16.0f}));

    // 2. Heavy Dynamic Wrecking Ball
    Engine::EntityID ball_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(ball_id, Engine::TagComponent{"WreckingBall"});
    m_entity_manager.add_component<Engine::TransformComponent>(ball_id, Engine::TransformComponent{glm::vec3(ball_pos, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(ball_id, Engine::SpriteComponent{m_wrecking_ball_texture.get(), glm::vec2(radius * 2.0f), SDL_Color{255, 255, 255, 255}});

    auto& ball_rb = m_entity_manager.add_component<Engine::RigidBodyComponent>(
        ball_id, Engine::RigidBodyComponent::Dynamic(1.0f, 0.6f, 0.3f, false)
    );
    ball_rb.mass = 4.0f;

    uint16_t ball_mask = Layer::GROUND | Layer::PLAYER | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        ball_id, Engine::ColliderComponent::Circle(radius, {0.0f, 0.0f}, false, Layer::DEFAULT, ball_mask)
    );

    // 3. Connect via Distance Joint
    m_entity_manager.add_component<Engine::JointComponent>(
        ball_id, Engine::JointComponent::Distance(anchor_id, {0.0f, 0.0f}, {0.0f, 0.0f}, -1.0f, 0.0f, 0.0f, false)
    );
}

void PlatformerDemoScene::create_slope(const glm::vec2& position, const glm::vec2& size, bool slope_up_right) {
    Engine::EntityID slope_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(slope_id, Engine::TagComponent{"Slope"});
    m_entity_manager.add_component<Engine::TransformComponent>(slope_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(slope_id, Engine::SpriteComponent{m_slope_texture.get(), size, SDL_Color{255, 255, 255, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(slope_id, Engine::RigidBodyComponent::Static(0.4f));

    uint16_t ground_mask = Layer::PLAYER | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        slope_id, Engine::ColliderComponent::Slope(size.x, size.y, slope_up_right, {0.0f, 0.0f}, false, Layer::GROUND, ground_mask)
    );
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

void PlatformerDemoScene::create_one_way_platform(const glm::vec2& position, const glm::vec2& size, SDL_Color color) {
    // 1. Single Box2D static collider for exact physics
    Engine::EntityID platform_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(platform_id, Engine::TagComponent{"OneWayPlatform"});
    m_entity_manager.add_component<Engine::TransformComponent>(platform_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(platform_id, Engine::RigidBodyComponent::Static());

    uint16_t ground_mask = Layer::PLAYER | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        platform_id, Engine::ColliderComponent::OneWayBox(size, {0.0f, 0.0f}, Layer::GROUND, ground_mask)
    );

    // 2. Modular repeating visual tiles across the width to avoid any stretching!
    float tile_w = 60.0f;
    int num_tiles = std::max(1, static_cast<int>(std::round(size.x / tile_w)));
    float actual_tile_w = size.x / static_cast<float>(num_tiles);
    float start_x = position.x - size.x * 0.5f + actual_tile_w * 0.5f;

    for (int i = 0; i < num_tiles; ++i) {
        float tile_x = start_x + static_cast<float>(i) * actual_tile_w;
        Engine::EntityID tile_id = m_entity_manager.create_entity();
        m_entity_manager.add_component<Engine::TransformComponent>(
            tile_id, Engine::TransformComponent{glm::vec3(tile_x, position.y, 0.0f)}
        );
        m_entity_manager.add_component<Engine::SpriteComponent>(
            tile_id, Engine::SpriteComponent{m_wood_platform_texture.get(), glm::vec2(actual_tile_w, size.y), SDL_Color{255, 255, 255, 255}}
        );
    }
}

void PlatformerDemoScene::create_platform(const glm::vec2& position, const glm::vec2& size, SDL_Color color) {
    // 1. Single Box2D static collider for exact physics
    Engine::EntityID platform_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(platform_id, Engine::TagComponent{"Platform"});
    m_entity_manager.add_component<Engine::TransformComponent>(platform_id, Engine::TransformComponent{glm::vec3(position, 0.0f)});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(platform_id, Engine::RigidBodyComponent::Static());

    uint16_t ground_mask = Layer::PLAYER | Layer::DEFAULT;
    m_entity_manager.add_component<Engine::ColliderComponent>(
        platform_id, Engine::ColliderComponent::Box(size, {0.0f, 0.0f}, false, Layer::GROUND, ground_mask)
    );

    // 2. Modular repeating visual tiles across the width to avoid any stretching!
    float tile_w = 64.0f;
    int num_tiles = std::max(1, static_cast<int>(std::round(size.x / tile_w)));
    float actual_tile_w = size.x / static_cast<float>(num_tiles);
    float start_x = position.x - size.x * 0.5f + actual_tile_w * 0.5f;

    for (int i = 0; i < num_tiles; ++i) {
        float tile_x = start_x + static_cast<float>(i) * actual_tile_w;
        Engine::EntityID tile_id = m_entity_manager.create_entity();
        m_entity_manager.add_component<Engine::TransformComponent>(
            tile_id, Engine::TransformComponent{glm::vec3(tile_x, position.y, 0.0f)}
        );
        m_entity_manager.add_component<Engine::SpriteComponent>(
            tile_id, Engine::SpriteComponent{m_block_texture.get(), glm::vec2(actual_tile_w, size.y), SDL_Color{255, 255, 255, 255}}
        );
    }
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
    
    auto& sprite = m_entity_manager.add_component<Engine::SpriteComponent>(
        coin_id, Engine::SpriteComponent{m_coin_texture.get(), glm::vec2(24.0f, 24.0f), SDL_Color{255, 215, 0, 255}}
    );
    sprite.has_outline = true;
    sprite.outline_color = SDL_Color{255, 255, 255, 220}; // Bright golden-white outline
    sprite.outline_thickness = 2.0f;
    sprite.outline_pulse = true;

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

    // Post-Processing Presets (Keys 1 - 6)
    if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_1)) {
        m_base_profile = Engine::PostProcessProfile::PresetDefault();
        ENGINE_LOG_INFO("[POST-PROCESS PRESET] Switched to 'Default' profile.");
    } else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_2)) {
        m_base_profile = Engine::PostProcessProfile::PresetRetroCRT();
        ENGINE_LOG_INFO("[POST-PROCESS PRESET] Switched to 'RetroCRT' scanline profile.");
    } else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_3)) {
        m_base_profile = Engine::PostProcessProfile::PresetUnderwater();
        ENGINE_LOG_INFO("[POST-PROCESS PRESET] Switched to 'Underwater' profile.");
    } else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_4)) {
        m_base_profile = Engine::PostProcessProfile::PresetCinematic();
        ENGINE_LOG_INFO("[POST-PROCESS PRESET] Switched to 'Cinematic' film-grain profile.");
    } else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_5)) {
        m_base_profile = Engine::PostProcessProfile::PresetNightVision();
        ENGINE_LOG_INFO("[POST-PROCESS PRESET] Switched to 'NightVision' high-gain profile.");
    } else if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_6)) {
        m_base_profile = Engine::PostProcessProfile::PresetDreamyBloom();
        ENGINE_LOG_INFO("[POST-PROCESS PRESET] Switched to 'DreamyBloom' profile.");
    }

    // 1. Toggle Debug Overlay with 'G' key & Toggle Laser Aim with 'L' key
    if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_G)) {
        m_show_debug_colliders = !m_show_debug_colliders;
        ENGINE_LOG_INFO("[PLATFORMER DEMO] Debug Wireframe Overlay: {}", m_show_debug_colliders ? "ON" : "OFF");
    }
    if (Engine::Input::is_key_just_pressed(SDL_SCANCODE_L)) {
        m_enable_laser = !m_enable_laser;
        ENGINE_LOG_INFO("[PLATFORMER DEMO] Laser Raycast Aim: {}", m_enable_laser ? "ON" : "OFF");
    }

    // 2. Real-time 2D Raycast Laser Pointer (Casts from Player to Mouse World Position)
    if (m_enable_laser && m_entity_manager.is_alive(m_player_id)) {
        glm::vec2 mouse_screen = Engine::Input::get_mouse_position();
        m_mouse_world_pos = m_camera.screen_to_world(mouse_screen);

        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        glm::vec2 player_center(player_tx.position.x, player_tx.position.y);

        glm::vec2 ray_dir = m_mouse_world_pos - player_center;
        m_last_raycast_hit = Engine::PhysicsSystem::raycast_direction(
            player_center, ray_dir, 1000.0f, Engine::CollisionCategory::ALL, true, m_player_id
        );
    }

    // 2.5 Interactive Object Hover Highlight (Hovering cursor over coins activates outline halo)
    {
        glm::vec2 mouse_screen = Engine::Input::get_mouse_position();
        glm::vec2 mouse_world = m_camera.screen_to_world(mouse_screen);

        m_entity_manager.for_each<Engine::TagComponent, Engine::TransformComponent, Engine::SpriteComponent>(
            [&mouse_world](Engine::EntityID id, const Engine::TagComponent& tag, const Engine::TransformComponent& tx, Engine::SpriteComponent& sprite) {
                if (tag.tag == "Coin") {
                    float dist = glm::distance(mouse_world, glm::vec2(tx.position.x, tx.position.y));
                    if (dist <= 26.0f) {
                        sprite.has_outline = true;
                        sprite.outline_color = SDL_Color{255, 255, 255, 255}; // Radiant white halo on hover!
                        sprite.outline_thickness = 4.0f;
                        sprite.outline_pulse = true;
                        sprite.size = glm::vec2(28.0f, 28.0f);
                    } else {
                        sprite.has_outline = false;
                        sprite.size = glm::vec2(24.0f, 24.0f);
                    }
                }
            }
        );
    }

    // 3. Right-Click or 'E' key: Fire / Detach Flexible Grappling Rope!
    if (Engine::Input::is_mouse_just_pressed(Engine::MouseButton::RIGHT) ||
        Engine::Input::is_key_just_pressed(SDL_SCANCODE_E)) {
        if (m_is_grappling) {
            detach_grapple();
        } else if (m_entity_manager.is_alive(m_player_id)) {
            glm::vec2 mouse_screen = Engine::Input::get_mouse_position();
            glm::vec2 target_world = m_camera.screen_to_world(mouse_screen);
            const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
            glm::vec2 player_pos(player_tx.position.x, player_tx.position.y);

            auto hit = Engine::PhysicsSystem::raycast(player_pos, target_world, Layer::GROUND, true, m_player_id);
            if (hit.hit) {
                attach_grapple(hit.point);
            }
        }
    }

    // 3. Left-Click (or 'F' key): Detonate Radial Physical Explosion at Mouse World Position!
    if (Engine::Input::is_mouse_just_pressed(Engine::MouseButton::LEFT) ||
        Engine::Input::is_key_just_pressed(SDL_SCANCODE_F)) {
        glm::vec2 mouse_screen = Engine::Input::get_mouse_position();
        glm::vec2 blast_pos = m_camera.screen_to_world(mouse_screen);

        float blast_radius = 240.0f;
        float blast_impulse = 750.0f; // Forceful, punchy blast impulse
        auto affected = Engine::PhysicsSystem::apply_explosion_force(
            m_entity_manager, blast_pos, blast_radius, blast_impulse, 0.35f, Engine::CollisionCategory::ALL, true
        );

        m_shockwaves.push_back({blast_pos, 0.0f, blast_radius, 0.35f, 0.0f});
        Engine::Presets::explosion(m_particle_system, blast_pos, 45, 18);

        // Trigger Post-Processing Flash, Bloom Pulse & Chromatic Aberration Punch
        Engine::PostProcessTweens::trigger_explosion_fx(m_post_process_pipeline, 7.5f, 1.2f, SDL_Color{255, 245, 200, 150});

        ENGINE_LOG_INFO("[EXPLOSION BLAST] Detonated at ({:.1f}, {:.1f})! Affected {} bodies.",
                        blast_pos.x, blast_pos.y, affected.size());
    }

    // Evaluate 2D Spatial Post-Processing Volumes
    glm::vec2 eval_pos = m_camera.get_position();
    if (m_entity_manager.is_alive(m_player_id) && m_entity_manager.has_component<Engine::TransformComponent>(m_player_id)) {
        const auto& p_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        eval_pos = glm::vec2(p_tx.position.x, p_tx.position.y);
    }

    // Diagnostic logging for 2D Post-Process Volume Atmospheric transitions
    static std::string s_last_active_vol = "";
    static float s_last_logged_weight = -1.0f;
    std::string current_vol_name = "";
    float current_vol_weight = 0.0f;

    for (const auto& vol : m_volume_manager.get_volumes()) {
        float w = vol.calculate_weight(eval_pos);
        if (w > 0.01f && w > current_vol_weight) {
            current_vol_weight = w;
            current_vol_name = vol.get_name();
        }
    }

    if (!current_vol_name.empty()) {
        if (std::abs(current_vol_weight - s_last_logged_weight) >= 0.15f || current_vol_name != s_last_active_vol) {
            s_last_active_vol = current_vol_name;
            s_last_logged_weight = current_vol_weight;
            ENGINE_LOG_INFO("[VOLUME TRANSITION] Inside '{}' | Atmospheric Blend: {:.0f}% | Player at ({:.0f}, {:.0f})",
                            current_vol_name, current_vol_weight * 100.0f, eval_pos.x, eval_pos.y);
        }
    } else if (!s_last_active_vol.empty()) {
        ENGINE_LOG_INFO("[VOLUME TRANSITION] Exited '{}' -> Smoothly returned to '{}' base profile.",
                        s_last_active_vol, m_base_profile.name);
        s_last_active_vol = "";
        s_last_logged_weight = 0.0f;
    }

    m_current_profile = m_volume_manager.evaluate(eval_pos, m_base_profile);
    m_current_profile.apply_to_pipeline(m_post_process_pipeline);

    // Update Post-Processing Pipeline & Tweens
    m_post_process_pipeline.update(delta_time);

    // Update Campfire Animation & Continuous Emission (Scalable via m_campfire_scale)
    m_campfire_time += delta_time;
    Engine::Presets::campfire(m_particle_system, m_campfire_pos + glm::vec2(0.0f, 6.0f * m_campfire_scale), m_campfire_scale);

    // Update Particle Engine & Expanding Shockwaves
    m_particle_system.update(delta_time);

    for (auto it = m_shockwaves.begin(); it != m_shockwaves.end();) {
        it->timer += delta_time;
        float progress = it->timer / it->lifetime;
        it->current_radius = it->max_radius * std::sin(progress * 1.57079f); // ease-out expansion
        if (it->timer >= it->lifetime) {
            it = m_shockwaves.erase(it);
        } else {
            ++it;
        }
    }

    // 2. Player Controls & Jump Physics
    if (m_entity_manager.is_alive(m_player_id) && m_entity_manager.has_component<Engine::RigidBodyComponent>(m_player_id)) {
        auto& rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(m_player_id);
        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);

        float move_speed = 320.0f;
        float move_x = 0.0f;

        if (Engine::Input::is_key_pressed(SDL_SCANCODE_A) || Engine::Input::is_key_pressed(SDL_SCANCODE_LEFT)) {
            move_x -= move_speed;
        }
        if (Engine::Input::is_key_pressed(SDL_SCANCODE_D) || Engine::Input::is_key_pressed(SDL_SCANCODE_RIGHT)) {
            move_x += move_speed;
        }

        if (m_is_grappling) {
            // Dynamic swinging force: allow the player to pump the pendulum with A/D!
            if (move_x != 0.0f) {
                rb.velocity.x += (move_x > 0.0f ? 600.0f : -600.0f) * delta_time;
                rb.velocity.x = std::clamp(rb.velocity.x, -550.0f, 550.0f);
            }
        } else {
            rb.velocity.x = move_x;
        }

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

        // Execute Jump if buffered and within coyote window (OR release grapple catapult jump!)
        if (m_jump_buffer_timer > 0.0f) {
            if (m_is_grappling) {
                detach_grapple();
                rb.velocity.y = std::max(rb.velocity.y + 260.0f, 480.0f);
                Engine::Presets::dust_puff(m_particle_system, glm::vec2(player_tx.position.x, player_tx.position.y - 28.0f), 12);
                m_jump_buffer_timer = 0.0f;
                m_coyote_timer = 0.0f;
                ENGINE_LOG_INFO("[GRAPPLE JUMP] Grapple Released with Catapult Momentum Boost!");
            } else if (m_coyote_timer > 0.0f) {
                rb.velocity.y = 520.0f;
                Engine::Presets::dust_puff(m_particle_system, glm::vec2(player_tx.position.x, player_tx.position.y - 28.0f), 10);
                m_jump_buffer_timer = 0.0f;
                m_coyote_timer = 0.0f;
                ENGINE_LOG_INFO("[PLATFORMER DEMO] Jump Applied! Velocity Y set to 520.0");
            }
        }

        // Drop-Down / Passthrough Mechanic (Pressing S or Down Arrow to drop through one-way platforms)
        if (Engine::Input::is_key_pressed(SDL_SCANCODE_S) || Engine::Input::is_key_pressed(SDL_SCANCODE_DOWN)) {
            m_drop_through_timer = 0.25f;
            Engine::PhysicsSystem::set_ignore_one_way(m_player_id, true);
        } else if (m_drop_through_timer > 0.0f) {
            m_drop_through_timer -= delta_time;
            if (m_drop_through_timer <= 0.0f) {
                Engine::PhysicsSystem::set_ignore_one_way(m_player_id, false);
            }
        }

        // 2.5. Animation State Machine (Idle, Run, Jump, Fall, Grapple + Flip X)
        if (m_knight_spritesheet_texture && m_entity_manager.has_component<Engine::SpriteComponent>(m_player_id)) {
            auto& player_sprite = m_entity_manager.get_component<Engine::SpriteComponent>(m_player_id);

            // Direction / Flip X
            if (move_x > 0.0f || rb.velocity.x > 15.0f) {
                m_player_facing_right = true;
            } else if (move_x < 0.0f || rb.velocity.x < -15.0f) {
                m_player_facing_right = false;
            }
            player_sprite.flip_x = !m_player_facing_right;

            // FSM State selection
            Engine::Animation2D* next_anim = &m_anim_idle;
            if (m_is_grappling) {
                next_anim = &m_anim_jump; // heroic swinging pose
            } else if (!is_physically_grounded && std::abs(rb.velocity.y) > 25.0f) {
                if (rb.velocity.y > 0.0f) {
                    next_anim = &m_anim_jump;
                } else {
                    next_anim = &m_anim_fall;
                }
            } else if (std::abs(rb.velocity.x) > 20.0f) {
                next_anim = &m_anim_run;
            } else {
                next_anim = &m_anim_idle;
            }

            if (m_current_player_anim != next_anim) {
                m_current_player_anim = next_anim;
                if (m_current_player_anim) {
                    m_current_player_anim->reset();
                    m_current_player_anim->play();
                }
            }

            if (m_current_player_anim) {
                m_current_player_anim->update(delta_time);
                player_sprite.use_src_rect = true;
                player_sprite.src_rect = m_current_player_anim->get_current_frame_rect();
                player_sprite.texture = m_knight_spritesheet_texture.get();
                player_sprite.size = glm::vec2(56.0f, 56.0f);
                player_sprite.color = SDL_Color{255, 255, 255, 255};
            }
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

    // 4. Interactive Mouse Wheel Zoom & Smooth Reset on Movement
    constexpr float DEFAULT_ZOOM = 0.76f;
    glm::vec2 scroll = Engine::Input::get_mouse_scroll();
    if (scroll.y != 0.0f) {
        m_target_zoom += scroll.y * 0.08f;
        m_target_zoom = std::clamp(m_target_zoom, 0.40f, 2.50f);
    }

    // Smooth Camera Follow & Zoom Interpolation
    if (m_entity_manager.is_alive(m_player_id)) {
        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        const auto& player_rb = m_entity_manager.get_component<Engine::RigidBodyComponent>(m_player_id);

        // If player moves horizontally or jumps/falls, smoothly reset zoom back to default
        bool is_moving = (Engine::Input::is_key_pressed(SDL_SCANCODE_A) || Engine::Input::is_key_pressed(SDL_SCANCODE_D) ||
                          Engine::Input::is_key_pressed(SDL_SCANCODE_LEFT) || Engine::Input::is_key_pressed(SDL_SCANCODE_RIGHT) ||
                          Engine::Input::is_key_pressed(SDL_SCANCODE_SPACE) || Engine::Input::is_key_pressed(SDL_SCANCODE_W) ||
                          std::abs(player_rb.velocity.x) > 25.0f || std::abs(player_rb.velocity.y) > 45.0f);
        if (is_moving) {
            m_target_zoom = DEFAULT_ZOOM;
        }

        // Smooth Zoom Lerp
        float cur_zoom = m_camera.get_zoom();
        float new_zoom = cur_zoom + (m_target_zoom - cur_zoom) * 5.5f * delta_time;
        m_camera.set_zoom(new_zoom);

        // Smooth Camera Follow
        glm::vec2 current_cam = m_camera.get_position();
        glm::vec2 target_cam = glm::vec2(player_tx.position.x * 0.45f, player_tx.position.y * 0.35f + 10.0f);
        m_camera.set_position(current_cam + (target_cam - current_cam) * 4.5f * delta_time);
    }
}

void PlatformerDemoScene::on_render(Engine::Renderer2D& renderer) {
    renderer.set_post_process_pipeline(&m_post_process_pipeline);
    renderer.begin_scene(m_camera);
    Engine::RenderSystem::render(m_entity_manager, renderer);

    if (m_show_debug_colliders) {
        Engine::RenderSystem::render_debug_colliders(m_entity_manager, renderer, m_circle_debug_texture.get());
    }

    // 2D Raycast Laser Pointer Visuals
    if (m_enable_laser && m_entity_manager.is_alive(m_player_id)) {
        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        glm::vec2 player_center(player_tx.position.x, player_tx.position.y);

        glm::vec2 hit_pt;
        if (m_last_raycast_hit.hit) {
            hit_pt = m_last_raycast_hit.point;
        } else {
            glm::vec2 dir = m_mouse_world_pos - player_center;
            float dir_len = glm::length(dir);
            hit_pt = player_center + ((dir_len > 0.001f) ? (dir / dir_len) : glm::vec2(1.0f, 0.0f)) * 1000.0f;
        }

        // 1. Draw glowing red laser beam line
        glm::vec2 delta = hit_pt - player_center;
        float beam_len = glm::length(delta);
        if (beam_len > 0.001f) {
            float beam_angle = -glm::degrees(std::atan2(delta.y, delta.x));
            glm::vec2 beam_mid = (player_center + hit_pt) * 0.5f;
            renderer.submit_sprite(
                nullptr,
                glm::vec3(beam_mid, 0.0f),
                glm::vec2(beam_len, 2.5f),
                beam_angle,
                SDL_Color{255, 50, 50, 220},
                nullptr,
                glm::vec2(0.5f, 0.5f),
                true
            );
        }

        // 2. If ray hits a surface: draw glowing impact reticle and cyan surface normal indicator
        if (m_last_raycast_hit.hit) {
            // Impact Dot
            renderer.submit_sprite(
                m_circle_debug_texture.get(),
                glm::vec3(m_last_raycast_hit.point, 0.0f),
                glm::vec2(14.0f, 14.0f),
                0.0f,
                SDL_Color{255, 230, 0, 255},
                nullptr,
                glm::vec2(0.5f, 0.5f),
                true
            );

            // Cyan Surface Normal Vector Line (24px length)
            glm::vec2 norm_end = m_last_raycast_hit.point + m_last_raycast_hit.normal * 24.0f;
            glm::vec2 norm_mid = (m_last_raycast_hit.point + norm_end) * 0.5f;
            float norm_angle = -glm::degrees(std::atan2(m_last_raycast_hit.normal.y, m_last_raycast_hit.normal.x));
            renderer.submit_sprite(
                nullptr,
                glm::vec3(norm_mid, 0.0f),
                glm::vec2(24.0f, 2.5f),
                norm_angle,
                SDL_Color{0, 255, 255, 255},
                nullptr,
                glm::vec2(0.5f, 0.5f),
                true
            );
        }
    }

    // 3. Render Expanding Explosion Shockwave Rings
    for (const auto& sw : m_shockwaves) {
        float alpha_factor = 1.0f - (sw.timer / sw.lifetime);
        uint8_t alpha = static_cast<uint8_t>(200.0f * alpha_factor);
        renderer.submit_sprite(
            m_circle_debug_texture.get(),
            glm::vec3(sw.center, 0.0f),
            glm::vec2(sw.current_radius * 2.0f),
            0.0f,
            SDL_Color{255, 120, 20, alpha},
            nullptr,
            glm::vec2(0.5f, 0.5f),
            true
        );
    }

    // 3.5. Render Campfire Showcase (2D Dynamic Light, Firewood, and Animated Flame Core)
    {
        // A. Dynamic 2D Warm Point Light Aura (Flickers and illuminates the ground at Z = -1.0)
        float flicker = (std::sin(m_campfire_time * 14.0f) * 10.0f + std::cos(m_campfire_time * 26.0f) * 6.0f) * m_campfire_scale;
        float light_radius = 240.0f * m_campfire_scale + flicker;
        uint8_t light_alpha = static_cast<uint8_t>(std::clamp(150.0f + flicker * 2.0f, 90.0f, 200.0f));
        renderer.submit_sprite(
            m_campfire_light_texture.get(),
            glm::vec3(m_campfire_pos + glm::vec2(0.0f, 12.0f * m_campfire_scale), -1.0f),
            glm::vec2(light_radius, light_radius * 0.75f),
            0.0f,
            SDL_Color{255, 130, 25, light_alpha},
            nullptr,
            glm::vec2(0.5f, 0.5f),
            false
        );

        // B. Rustic Firewood Logs Base (Behind player at Z = -0.2)
        renderer.submit_sprite(
            m_campfire_logs_texture.get(),
            glm::vec3(m_campfire_pos, -0.2f),
            glm::vec2(52.0f, 26.0f) * m_campfire_scale,
            0.0f,
            SDL_Color{255, 255, 255, 255},
            nullptr,
            glm::vec2(0.5f, 0.5f),
            false
        );

        // C. Animated Incandescent Flame Core (Pulsing & swaying at Z = -0.1)
        float flame_sway = std::sin(m_campfire_time * 16.0f) * 2.0f * m_campfire_scale;
        float flame_pulse = std::cos(m_campfire_time * 22.0f) * 3.0f * m_campfire_scale;
        renderer.submit_sprite(
            m_flame_core_texture.get(),
            glm::vec3(m_campfire_pos + glm::vec2(flame_sway, 14.0f * m_campfire_scale), -0.1f),
            glm::vec2(22.0f * m_campfire_scale + flame_pulse, 34.0f * m_campfire_scale - flame_pulse * 0.5f),
            flame_sway * 2.0f,
            SDL_Color{255, 225, 75, 245},
            nullptr,
            glm::vec2(0.5f, 0.2f),
            false
        );
    }

    // 4. Render Active 2D Particles (Sparks, Smoke, Dust, Starbursts, Campfire Embers)
    m_particle_system.render(renderer);

    renderer.end_scene();
}

void PlatformerDemoScene::attach_grapple(const glm::vec2& target_point) {
    detach_grapple();

    m_grapple_anchor_id = m_entity_manager.create_entity();
    m_entity_manager.add_component<Engine::TagComponent>(m_grapple_anchor_id, Engine::TagComponent{"GrappleAnchor"});
    m_entity_manager.add_component<Engine::TransformComponent>(m_grapple_anchor_id, Engine::TransformComponent{glm::vec3(target_point, 0.0f)});
    m_entity_manager.add_component<Engine::SpriteComponent>(m_grapple_anchor_id, Engine::SpriteComponent{m_circle_debug_texture.get(), glm::vec2(14.0f, 14.0f), SDL_Color{255, 230, 0, 255}});
    m_entity_manager.add_component<Engine::RigidBodyComponent>(m_grapple_anchor_id, Engine::RigidBodyComponent::Static());
    m_entity_manager.add_component<Engine::ColliderComponent>(m_grapple_anchor_id, Engine::ColliderComponent::Circle(7.0f));

    // Spawn metal impact ricochet sparks!
    Engine::Presets::impact_sparks(m_particle_system, target_point, 14);

    if (m_entity_manager.is_alive(m_player_id) && m_entity_manager.has_component<Engine::TransformComponent>(m_player_id)) {
        const auto& player_tx = m_entity_manager.get_component<Engine::TransformComponent>(m_player_id);
        float rope_length = glm::distance(glm::vec2(player_tx.position.x, player_tx.position.y), target_point);

        // Attach Slack Rope Joint to Player!
        m_entity_manager.add_component<Engine::JointComponent>(
            m_player_id, Engine::JointComponent::Rope(m_grapple_anchor_id, rope_length)
        );
        m_is_grappling = true;
        ENGINE_LOG_INFO("[GRAPPLE ROPE] Attached rope to ({:.1f}, {:.1f}) with max_length={:.1f}px!",
                        target_point.x, target_point.y, rope_length);
    }
}

void PlatformerDemoScene::detach_grapple() {
    if (m_is_grappling) {
        Engine::PhysicsSystem::destroy_joint(m_player_id);
        if (m_entity_manager.is_alive(m_player_id) && m_entity_manager.has_component<Engine::JointComponent>(m_player_id)) {
            m_entity_manager.remove_component<Engine::JointComponent>(m_player_id);
        }
        if (m_grapple_anchor_id != 0 && m_entity_manager.is_alive(m_grapple_anchor_id)) {
            m_entity_manager.destroy_entity(m_grapple_anchor_id);
            m_grapple_anchor_id = 0;
        }
        m_is_grappling = false;
        ENGINE_LOG_INFO("[GRAPPLE ROPE] Detached and removed grapple rope!");
    }
}

} // namespace Sandbox
