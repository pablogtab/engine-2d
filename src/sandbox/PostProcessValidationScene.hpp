#pragma once

#include "../core/AssetHandle.hpp"
#include "../scene/Camera2D.hpp"
#include "../scene/Scene.hpp"
#include "../renderer/Font.hpp"
#include "../renderer/Texture.hpp"
#include "../renderer/postprocess/PostProcessPipeline.hpp"
#include "../renderer/postprocess/PostProcessProfile.hpp"
#include "../renderer/material/Material.hpp"
#include <memory>
#include <vector>

namespace Engine {
class ILocalEffectPass;
}

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
    Engine::PostProcessPipeline m_material_pipeline;
    Engine::PostProcessProfile m_profile;
    std::unique_ptr<Engine::Texture> m_white_texture;
    std::unique_ptr<Engine::Texture> m_grid_texture;
    std::unique_ptr<Engine::Font> m_font;
    std::shared_ptr<Engine::Material> m_night_vision_material;
    Engine::AssetHandle m_local_effect_profile_handle{};
    std::vector<std::shared_ptr<Engine::ILocalEffectPass>> m_local_effect_passes;
    bool m_material_pass_registered{false};
    bool m_local_effects_registered{false};
    bool m_local_effects_registration_attempted{false};
    int m_mode{9};
    bool m_post_process_enabled{false};
    bool m_log_next_render{true};
    bool m_log_composition_metrics{false};
};

} // namespace Sandbox
