#include "StressTestScene.hpp"
#include "../renderer/Renderer2D.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace Sandbox {

StressTestScene::StressTestScene(SDL_Renderer *renderer, size_t particle_count)
    : Engine::Scene("GPU/CPU Stress Test (3,000 Sprites)"),
      m_sdl_renderer(renderer), m_camera(1280.0f, 720.0f) {

  m_particles.resize(particle_count);
}

void StressTestScene::on_attach() {
  m_particle_texture = std::unique_ptr<Engine::Texture>(
      Engine::Texture::create_circle(m_sdl_renderer, 64, 255, 255, 255));

  // Initialize particles across 3D space
  for (auto &p : m_particles) {
    p.position.x = static_cast<float>((rand() % 1600) - 800);
    p.position.y = static_cast<float>((rand() % 1000) - 500);
    p.position.z = static_cast<float>((rand() % 100) - 50); // Z from -50 to +50

    p.velocity.x = static_cast<float>((rand() % 100) - 50);
    p.velocity.y = static_cast<float>((rand() % 100) - 50);
    p.size = glm::vec2(16.0f + (rand() % 16));

    p.color.r = static_cast<uint8_t>(50 + rand() % 205);
    p.color.g = static_cast<uint8_t>(50 + rand() % 205);
    p.color.b = static_cast<uint8_t>(50 + rand() % 205);
    p.color.a = 255;
  }

  std::cout << "[STRESS TEST SCENE] Active: Rendering " << m_particles.size()
            << " sprites simultaneously. Press TAB to switch scenes."
            << std::endl;
}

void StressTestScene::on_resize(int width, int height) {
  m_camera.set_viewport_size(1280.0f, 720.0f);
}

void StressTestScene::on_update(float delta_time) {
  m_total_time += delta_time;

  // Orbit camera slowly
  m_camera.set_position(glm::vec2(std::sin(m_total_time * 0.5f) * 100.0f,
                                  std::cos(m_total_time * 0.5f) * 50.0f));

  // Update particles
  for (auto &p : m_particles) {
    p.position.x += p.velocity.x * delta_time;
    p.position.y += p.velocity.y * delta_time;

    // Bounce at boundaries
    if (p.position.x < -800.0f || p.position.x > 800.0f)
      p.velocity.x *= -1.0f;
    if (p.position.y < -500.0f || p.position.y > 500.0f)
      p.velocity.y *= -1.0f;
  }
}

void StressTestScene::on_render(Engine::Renderer2D &renderer) {
  renderer.begin_scene(m_camera);

  for (const auto &p : m_particles) {
    renderer.submit_sprite(m_particle_texture.get(), p.position, p.size, 0.0f,
                           p.color);
  }

  renderer.end_scene();
}

} // namespace Sandbox
