#include "AnimatedSpriteDemoScene.hpp"
#include "../renderer/Renderer2D.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <cmath>

namespace Sandbox {

AnimatedSpriteDemoScene::AnimatedSpriteDemoScene(SDL_Renderer* renderer)
    : Engine::Scene("2D Animated Sprites & Asset Loader"),
      m_sdl_renderer(renderer),
      m_camera(1280.0f, 720.0f) {}

Engine::Texture* AnimatedSpriteDemoScene::generate_procedural_spritesheet() {
    // Generates a 6-column x 3-row SpriteSheet (frame size: 64x64 px -> texture size: 384x192 px)
    const int frame_w = 64;
    const int frame_h = 64;
    const int cols = 6;
    const int rows = 3;
    const int tex_w = frame_w * cols;
    const int tex_h = frame_h * rows;

    std::vector<uint32_t> pixels(tex_w * tex_h, 0); // Transparent background

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int frame_idx = r * cols + c;
            int start_x = c * frame_w;
            int start_y = r * frame_h;

            // Base color depending on row: Row 0 = Idle (Blue/Cyan), Row 1 = Run (Green/Yellow), Row 2 = Spin (Magenta/Orange)
            uint8_t base_r = (r == 2) ? 255 : ((r == 1) ? 100 : 40);
            uint8_t base_g = (r == 1) ? 220 : ((r == 2) ? 60 : 180);
            uint8_t base_b = (r == 0) ? 255 : ((r == 2) ? 220 : 60);

            // Draw animated character body inside frame
            for (int y = 0; y < frame_w; ++y) {
                for (int x = 0; x < frame_h; ++x) {
                    float cx = x - frame_w * 0.5f;
                    float cy = y - frame_h * 0.5f;

                    // Bounce offset per frame
                    float anim_phase = (float)c / (float)cols * 3.14159f * 2.0f;
                    if (r == 1) cy += std::sin(anim_phase) * 6.0f; // Running bounce
                    if (r == 2) {
                        // Rotating offset
                        float rot_x = cx * std::cos(anim_phase) - cy * std::sin(anim_phase);
                        float rot_y = cx * std::sin(anim_phase) + cy * std::cos(anim_phase);
                        cx = rot_x; cy = rot_y;
                    }

                    float dist_sq = cx * cx + cy * cy;

                    // Outer body circle
                    if (dist_sq <= 22.0f * 22.0f) {
                        float dist = std::sqrt(dist_sq);
                        float alpha_f = (dist > 20.5f) ? (22.0f - dist) / 1.5f : 1.0f;
                        uint8_t a = static_cast<uint8_t>(255 * std::clamp(alpha_f, 0.0f, 1.0f));

                        // Eye dot on right side
                        if (std::abs(cx - 8.0f) < 4.0f && std::abs(cy - (-4.0f)) < 4.0f) {
                            pixels[(start_y + y) * tex_w + (start_x + x)] = (255 << 24) | (255 << 16) | (255 << 8) | a;
                        } else {
                            pixels[(start_y + y) * tex_w + (start_x + x)] = (base_r << 24) | (base_g << 16) | (base_b << 8) | a;
                        }
                    }
                }
            }
        }
    }

    return new Engine::Texture(m_sdl_renderer, tex_w, tex_h, pixels.data());
}

void AnimatedSpriteDemoScene::on_attach() {
    // Attempt to load real PNG asset from assets/textures/ folder
    Engine::Texture* file_tex = Engine::Texture::load_from_file(m_sdl_renderer, "assets/textures/player.png");

    if (file_tex) {
        m_spritesheet_texture = std::shared_ptr<Engine::Texture>(file_tex);
        // Created a 4-col x 4-row PNG sprite sheet
        m_spritesheet = std::make_unique<Engine::SpriteSheet>(m_spritesheet_texture.get(), 4, 4);

        m_anim_idle.set_spritesheet(m_spritesheet.get());
        m_anim_idle.set_clip(0, 3, 6.0f, true); // Row 0

        m_anim_run.set_spritesheet(m_spritesheet.get());
        m_anim_run.set_clip(4, 7, 10.0f, true); // Row 1

        m_anim_spin.set_spritesheet(m_spritesheet.get());
        m_anim_spin.set_clip(8, 15, 14.0f, true); // Rows 2 & 3
    } else {
        // Fallback to procedural generator if PNG asset is missing
        m_spritesheet_texture = std::shared_ptr<Engine::Texture>(generate_procedural_spritesheet());
        m_spritesheet = std::make_unique<Engine::SpriteSheet>(m_spritesheet_texture.get(), 6, 3);

        m_anim_idle.set_spritesheet(m_spritesheet.get());
        m_anim_idle.set_clip(0, 5, 8.0f, true);

        m_anim_run.set_spritesheet(m_spritesheet.get());
        m_anim_run.set_clip(6, 11, 12.0f, true);

        m_anim_spin.set_spritesheet(m_spritesheet.get());
        m_anim_spin.set_clip(12, 17, 16.0f, true);
    }

    m_bg_texture = std::shared_ptr<Engine::Texture>(
        Engine::Texture::create_grid(m_sdl_renderer, 64, 40, 50, 70)
    );
    m_tree_texture = std::shared_ptr<Engine::Texture>(
        Engine::Texture::create_circle(m_sdl_renderer, 64, 50, 160, 90)
    );

    m_current_anim = &m_anim_idle;

    std::cout << "\n=======================================================" << std::endl;
    std::cout << " [ANIMATED SPRITE DEMO ACTIVE]" << std::endl;
    std::cout << " - WASD / Arrow Keys : Move Character in 2.5D Space" << std::endl;
    std::cout << " - SPACE / SHIFT    : Trigger Spin / Jump Animation" << std::endl;
    std::cout << " - TAB               : Switch Test Scenes" << std::endl;
    std::cout << "=======================================================\n" << std::endl;
}

void AnimatedSpriteDemoScene::on_resize(int width, int height) {
    m_camera.set_viewport_size(1280.0f, 720.0f);
}

void AnimatedSpriteDemoScene::on_update(float delta_time) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float speed = 250.0f * delta_time;

    glm::vec2 move{0.0f, 0.0f};
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    move.y -= speed;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  move.y += speed;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  move.x -= speed;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) move.x += speed;

    bool is_moving = (move.x != 0.0f || move.y != 0.0f);
    if (move.x < 0.0f) m_facing_right = false;
    if (move.x > 0.0f) m_facing_right = true;

    m_player_pos.x += move.x;
    m_player_pos.y += move.y;

    // Trigger Spin / Jump on Space or Shift
    if (keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_LSHIFT]) {
        m_spin_timer = 0.5f;
    }

    if (m_spin_timer > 0.0f) {
        m_spin_timer -= delta_time;
        m_current_anim = &m_anim_spin;
    } else if (is_moving) {
        m_current_anim = &m_anim_run;
    } else {
        m_current_anim = &m_anim_idle;
    }

    if (m_current_anim) {
        m_current_anim->update(delta_time);
    }

    // Camera smoothly follows player
    glm::vec2 current_cam = m_camera.get_position();
    glm::vec2 target_cam = glm::vec2(m_player_pos.x, m_player_pos.y);
    m_camera.set_position(current_cam + (target_cam - current_cam) * 5.0f * delta_time);
}

void AnimatedSpriteDemoScene::on_render(Engine::Renderer2D& renderer) {
    renderer.begin_scene(m_camera);

    // Background Grid
    for (int x = -4; x <= 4; ++x) {
        for (int y = -3; y <= 3; ++y) {
            renderer.submit_sprite(
                m_bg_texture.get(),
                glm::vec3(x * 160.0f, y * 160.0f, -40.0f),
                glm::vec2(150.0f, 150.0f)
            );
        }
    }

    // Trees at Z = 0
    renderer.submit_sprite(m_tree_texture.get(), glm::vec3(-200.0f, -80.0f, 0.0f), glm::vec2(100.0f, 120.0f));
    renderer.submit_sprite(m_tree_texture.get(), glm::vec3(180.0f, 100.0f, 0.0f), glm::vec2(110.0f, 130.0f));

    // Player Animated Sprite
    if (m_current_anim) {
        SDL_FRect src_rect = m_current_anim->get_current_frame_rect();
        renderer.submit_sprite(
            m_current_anim->get_texture(),
            m_player_pos,
            glm::vec2(64.0f, 64.0f),
            0.0f,
            {255, 255, 255, 255},
            &src_rect
        );
    }

    renderer.end_scene();
}

} // namespace Sandbox
