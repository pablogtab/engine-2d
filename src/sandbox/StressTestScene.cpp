#include "StressTestScene.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../core/Log.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>

namespace Sandbox {

StressTestScene::StressTestScene(SDL_Renderer *renderer, size_t particle_count)
    : Engine::Scene("GPU/CPU Stress Test"),
      m_sdl_renderer(renderer), m_camera(1280.0f, 720.0f) {

  set_particle_count(particle_count);
}

void StressTestScene::set_particle_count(size_t count) {
  m_particles.clear();
  m_particles.resize(count);

  bool is_galaxy_mode = (count >= 30000);

  for (size_t i = 0; i < count; ++i) {
    auto &p = m_particles[i];

    if (is_galaxy_mode) {
      // Spiral Galaxy distribution for 30K+ micro-particles
      float arm_angle = (i % 4) * (3.14159f * 0.5f);
      float dist_factor = std::sqrt(static_cast<float>(i) / count);
      float dist = dist_factor * 1200.0f;
      float angle = dist_factor * 12.0f + arm_angle + (rand() % 100) * 0.005f;

      p.position.x = std::cos(angle) * dist + (rand() % 40 - 20);
      p.position.y = std::sin(angle) * dist + (rand() % 40 - 20);
      p.position.z = static_cast<float>((rand() % 80) - 40);

      // Tangential orbital velocity
      float speed = 40.0f + (rand() % 40);
      p.velocity.x = -std::sin(angle) * speed;
      p.velocity.y =  std::cos(angle) * speed;

      // Micro-particle sizing (2px to 6px) to avoid overdraw clutter
      p.size = glm::vec2(2.0f + (rand() % 5));

      // Cosmic galaxy palette (Cyan, Violet, Gold, Bright White)
      int color_theme = i % 4;
      if (color_theme == 0)      p.color = {100, 220, 255, 255}; // Cyan
      else if (color_theme == 1) p.color = {240, 100, 255, 255}; // Magenta/Violet
      else if (color_theme == 2) p.color = {255, 210, 100, 255}; // Gold
      else                       p.color = {255, 255, 255, 255}; // White star
    } else {
      // Standard particle test (3K to 10K)
      p.position.x = static_cast<float>((rand() % 2400) - 1200);
      p.position.y = static_cast<float>((rand() % 1400) - 700);
      p.position.z = static_cast<float>((rand() % 100) - 50);

      p.velocity.x = static_cast<float>((rand() % 120) - 60);
      p.velocity.y = static_cast<float>((rand() % 120) - 60);
      p.size = glm::vec2(16.0f + (rand() % 16));

      p.color.r = static_cast<uint8_t>(50 + rand() % 205);
      p.color.g = static_cast<uint8_t>(50 + rand() % 205);
      p.color.b = static_cast<uint8_t>(50 + rand() % 205);
      p.color.a = 255;
    }
  }
}

void StressTestScene::on_attach() {
  m_particle_texture = std::unique_ptr<Engine::Texture>(
      Engine::Texture::create_circle(m_sdl_renderer, 64, 255, 255, 255));

  ENGINE_LOG_INFO("=======================================================");
  ENGINE_LOG_INFO(" [STRESS TEST SCENE ACTIVE] Particles: {}", m_particles.size());
  ENGINE_LOG_INFO(" - Press 1 : 3,000 Sprites");
  ENGINE_LOG_INFO(" - Press 2 : 10,000 Sprites");
  ENGINE_LOG_INFO(" - Press 3 : 30,000 Sprites");
  ENGINE_LOG_INFO(" - Press 4 : 75,000 Sprites");
  ENGINE_LOG_INFO(" - Press 5 : 150,000 Sprites");
  ENGINE_LOG_INFO("=======================================================");
}

void StressTestScene::on_resize(int width, int height) {
  m_camera.set_viewport_size(1280.0f, 720.0f);
}

void StressTestScene::on_update(float delta_time) {
  m_total_time += delta_time;
  m_accumulated_time += delta_time;
  m_accumulated_frames++;

  // Orbit camera slowly
  m_camera.set_position(glm::vec2(std::sin(m_total_time * 0.5f) * 100.0f,
                                  std::cos(m_total_time * 0.5f) * 50.0f));

  // Dynamic particle count hotkeys
  const bool* keys = SDL_GetKeyboardState(nullptr);
  if (keys[SDL_SCANCODE_1] && m_particles.size() != 3000)   { set_particle_count(3000);   ENGINE_LOG_INFO("Set Stress Test to 3,000 Sprites"); }
  if (keys[SDL_SCANCODE_2] && m_particles.size() != 10000)  { set_particle_count(10000);  ENGINE_LOG_INFO("Set Stress Test to 10,000 Sprites"); }
  if (keys[SDL_SCANCODE_3] && m_particles.size() != 30000)  { set_particle_count(30000);  ENGINE_LOG_INFO("Set Stress Test to 30,000 Sprites"); }
  if (keys[SDL_SCANCODE_4] && m_particles.size() != 75000)  { set_particle_count(75000);  ENGINE_LOG_INFO("Set Stress Test to 75,000 Sprites"); }
  if (keys[SDL_SCANCODE_5] && m_particles.size() != 150000) { set_particle_count(150000); ENGINE_LOG_INFO("Set Stress Test to 150,000 Sprites"); }

  // Update particles
  for (auto &p : m_particles) {
    p.position.x += p.velocity.x * delta_time;
    p.position.y += p.velocity.y * delta_time;

    // Bounce at boundaries
    if (p.position.x < -1200.0f || p.position.x > 1200.0f)
      p.velocity.x *= -1.0f;
    if (p.position.y < -700.0f || p.position.y > 700.0f)
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

  // Print accurate real-time telemetry every 1.5 seconds
  if (m_accumulated_time >= 1.5f && m_accumulated_frames > 0) {
    int fps = static_cast<int>(m_accumulated_frames / m_accumulated_time);
    float frame_ms = (m_accumulated_time / m_accumulated_frames) * 1000.0f;

    ENGINE_LOG_INFO("[PERFORMANCE] Active: {} | Rendered: {} | Culled: {} | Draw Calls: {} | FPS: {} ({:.2f} ms/frame)",
                    m_particles.size(),
                    renderer.get_rendered_sprite_count(),
                    renderer.get_culled_sprite_count(),
                    renderer.get_draw_call_count(),
                    fps,
                    frame_ms);

    m_accumulated_time = 0.0f;
    m_accumulated_frames = 0;
  }
}

} // namespace Sandbox
