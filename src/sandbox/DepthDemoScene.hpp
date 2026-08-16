#pragma once

#include "../scene/Scene.hpp"
#include "../scene/Camera2D.hpp"
#include "../renderer/Texture.hpp"
#include <memory>
#include <glm/glm.hpp>

namespace Sandbox {

class DepthDemoScene : public Engine::Scene {
public:
    DepthDemoScene(SDL_Renderer* renderer);

    void on_attach() override;
    void on_update(float delta_time) override;
    void on_render(Engine::Renderer2D& renderer) override;
    void on_resize(int width, int height, float dpi_scale = 1.0f) override;

private:
    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::Camera2D m_camera;

    std::unique_ptr<Engine::Texture> m_bg_texture;
    std::unique_ptr<Engine::Texture> m_mid_texture;
    std::unique_ptr<Engine::Texture> m_player_texture;
    std::unique_ptr<Engine::Texture> m_tree_texture;
    std::unique_ptr<Engine::Texture> m_fg_texture;

    glm::vec3 m_player_pos{0.0f, 0.0f, 0.0f};
    float m_player_anim_time{0.0f};
};

} // namespace Sandbox
