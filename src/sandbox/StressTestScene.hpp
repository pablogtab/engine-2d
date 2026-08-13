#pragma once

#include "../scene/Scene.hpp"
#include "../scene/Camera2D.hpp"
#include "../renderer/Texture.hpp"
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace Sandbox {

struct Particle {
    glm::vec3 position;
    glm::vec2 velocity;
    glm::vec2 size;
    SDL_Color color;
};

class StressTestScene : public Engine::Scene {
public:
    explicit StressTestScene(SDL_Renderer* renderer, size_t particle_count = 3000);

    void on_attach() override;
    void on_update(float delta_time) override;
    void on_render(Engine::Renderer2D& renderer) override;
    void on_resize(int width, int height) override;

private:
    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::Camera2D m_camera;
    std::unique_ptr<Engine::Texture> m_particle_texture;
    std::vector<Particle> m_particles;
    float m_total_time{0.0f};
};

} // namespace Sandbox
