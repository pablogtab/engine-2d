#pragma once

#include "../scene/Camera2D.hpp"
#include "../scene/Scene.hpp"
#include "../renderer/Font.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/postprocess/PostProcessPipeline.hpp"
#include "../renderer/postprocess/PostProcessProfile.hpp"
#include <memory>

namespace Sandbox {

class PostProcessValidationScene final : public Engine::Scene {
public:
    explicit PostProcessValidationScene(SDL_Renderer* renderer);

    void on_attach() override;
    void on_detach() override;
    void on_update(float delta_time) override;
    void on_render(Engine::Renderer2D& renderer) override;
    void on_viewport_resize(
        Engine::RenderViewKind view,
        const Engine::RenderViewConfiguration& configuration
    ) override;

    [[nodiscard]] Engine::PostProcessPipeline* get_post_process_pipeline() override {
        return &m_pipeline;
    }
    [[nodiscard]] Engine::PostProcessProfile* get_editable_post_process_profile() override {
        return &m_profile;
    }

private:
    void select_mode(int mode);

    SDL_Renderer* m_sdl_renderer{nullptr};
    Engine::Camera2D m_camera;
    Engine::RenderViewConfiguration m_view_configuration;
    Engine::PostProcessPipeline m_pipeline;
    Engine::PostProcessProfile m_profile;
    std::unique_ptr<Engine::Texture> m_white_texture;
    std::unique_ptr<Engine::Texture> m_grid_texture;
    std::unique_ptr<Engine::Font> m_font;
    int m_mode{5};
    bool m_post_process_enabled{true};
    bool m_log_next_render{true};
};

} // namespace Sandbox
