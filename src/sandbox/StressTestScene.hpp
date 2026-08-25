#pragma once

#include "../scene/CustomScene.hpp"
#include "../scene/Camera2D.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../core/ThreadPool.hpp"
#include "../debug/StressScenarios.hpp"
#include <vector>
#include <memory>
#include <string>
#include <glm/glm.hpp>

namespace Sandbox {

struct Particle {
    glm::vec3 position;
    glm::vec2 velocity;
    glm::vec2 size;
    SDL_Color color;
};

class StressTestScene : public Engine::CustomScene {
public:
    explicit StressTestScene(SDL_Renderer* renderer, Engine::ThreadPool* thread_pool = nullptr, std::size_t scenario_index = 0);

    void on_attach() override;
    void on_update(float delta_time) override;
    void on_render(Engine::Renderer2D& renderer) override;
    void on_viewport_resize(Engine::RenderViewKind view, const Engine::RenderViewConfiguration& configuration) override;

    void set_scenario(const Engine::StressScenario& scenario);

private:
    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::ThreadPool* m_thread_pool{nullptr};
    Engine::Camera2D m_camera;
    std::unique_ptr<Engine::Texture> m_particle_texture;
    std::vector<Particle> m_particles;
    std::vector<Engine::SpriteCommand> m_sprite_batch;
    std::string m_scenario_name;
    float m_total_time{0.0f};
    float m_accumulated_time{0.0f};
    int m_accumulated_frames{0};
};

} // namespace Sandbox
