#include "DepthDemoScene.hpp"
#include "../renderer/Renderer2D.hpp"
#include <SDL3/SDL.h>
#include <iostream>
#include <cmath>

namespace Sandbox {

DepthDemoScene::DepthDemoScene(SDL_Renderer* renderer)
    : Engine::Scene("2.5D Depth & Parallax Test"),
      m_sdl_renderer(renderer),
      m_camera(1280.0f, 720.0f) {}

void DepthDemoScene::on_attach() {
    // Generate procedural textures for scene layers
    m_bg_texture = std::unique_ptr<Engine::Texture>(Engine::Texture::create_solid(m_sdl_renderer, 64, 64, 40, 48, 72));
    m_mid_texture = std::unique_ptr<Engine::Texture>(Engine::Texture::create_grid(m_sdl_renderer, 64, 60, 140, 90));
    m_player_texture = std::unique_ptr<Engine::Texture>(Engine::Texture::create_circle(m_sdl_renderer, 64, 240, 80, 80));
    m_tree_texture = std::unique_ptr<Engine::Texture>(Engine::Texture::create_circle(m_sdl_renderer, 64, 40, 180, 80));
    m_fg_texture = std::unique_ptr<Engine::Texture>(Engine::Texture::create_circle(m_sdl_renderer, 64, 255, 220, 100, 180));

    std::cout << "[DEPTH SCENE] Controls: WASD/Arrows to move Camera. Press TAB to switch test scene." << std::endl;
}

void DepthDemoScene::on_resize(int width, int height) {
    m_camera.set_viewport_size(1280.0f, 720.0f);
}

void DepthDemoScene::on_update(float delta_time) {
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float speed = 400.0f * delta_time;

    glm::vec2 cam_move{0.0f, 0.0f};
    if (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP])    cam_move.y -= speed;
    if (keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN])  cam_move.y += speed;
    if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT])  cam_move.x -= speed;
    if (keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) cam_move.x += speed;

    m_camera.move(cam_move);

    // Smooth automatic player animation
    m_player_anim_time += delta_time;
    m_player_pos.x = std::sin(m_player_anim_time * 2.0f) * 150.0f;
    m_player_pos.y = std::cos(m_player_anim_time * 1.5f) * 80.0f;
}

void DepthDemoScene::on_render(Engine::Renderer2D& renderer) {
    renderer.begin_scene(m_camera);

    // 1. Distant Background Layer (Z = -60.0f)
    for (int x = -5; x <= 5; ++x) {
        for (int y = -3; y <= 3; ++y) {
            renderer.submit_sprite(
                m_bg_texture.get(),
                glm::vec3(x * 180.0f, y * 180.0f, -60.0f),
                glm::vec2(160.0f, 160.0f)
            );
        }
    }

    // 2. Midground Layer (Z = -20.0f)
    for (int i = -3; i <= 3; ++i) {
        renderer.submit_sprite(
            m_mid_texture.get(),
            glm::vec3(i * 220.0f, -120.0f, -20.0f),
            glm::vec2(120.0f, 120.0f)
        );
    }

    // 3. Gameplay Plane (Z = 0.0f) with Y-Sorting
    renderer.submit_sprite(m_tree_texture.get(), glm::vec3(-150.0f, -50.0f, 0.0f), glm::vec2(80.0f, 100.0f));
    renderer.submit_sprite(m_tree_texture.get(), glm::vec3(100.0f, 60.0f, 0.0f), glm::vec2(90.0f, 110.0f));
    renderer.submit_sprite(m_tree_texture.get(), glm::vec3(0.0f, 20.0f, 0.0f), glm::vec2(85.0f, 105.0f));

    // Animated Player
    renderer.submit_sprite(
        m_player_texture.get(),
        m_player_pos,
        glm::vec2(48.0f, 48.0f),
        0.0f,
        {255, 220, 100, 255}
    );

    // 4. Foreground Layer (Z = +40.0f)
    for (int i = 0; i < 5; ++i) {
        float offset_x = std::sin(m_player_anim_time + i) * 200.0f;
        float offset_y = std::cos(m_player_anim_time * 0.8f + i) * 150.0f;
        renderer.submit_sprite(
            m_fg_texture.get(),
            glm::vec3(i * 120.0f - 240.0f + offset_x, offset_y, 40.0f),
            glm::vec2(40.0f, 40.0f)
        );
    }

    renderer.end_scene();
}

} // namespace Sandbox
