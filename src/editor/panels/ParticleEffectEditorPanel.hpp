#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../particles/ParticleEffectAsset.hpp"
#include "../../particles/ParticleSystem2D.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include "../../scene/Camera2D.hpp"
#include <memory>
#include <optional>
#include <string>

namespace Engine {

class EditorCommandHistory;

class ParticleEffectEditorPanel : public EditorPanel {
public:
    explicit ParticleEffectEditorPanel(
        SDL_Renderer* renderer,
        std::shared_ptr<EditorCommandHistory> command_history = nullptr
    );
    ~ParticleEffectEditorPanel() override;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = std::move(ch); }

    void open_asset(AssetHandle handle);
    void on_render_resources_invalidated() override;

private:
    void render_preview(const ParticleEffectAsset& asset, AssetDatabase& database);
    void commit_draft_gesture();
    void destroy_preview();

    std::string m_name{"Particle Effect Editor"};
    std::shared_ptr<EditorCommandHistory> m_command_history;

    AssetHandle m_current_handle{};
    std::optional<ParticleEffectAsset> m_current_asset;
    std::optional<ParticleEffectAsset> m_saved_asset;
    std::string m_current_filepath;
    bool m_is_dirty{false};
    std::optional<ParticleEffectAsset> m_edit_before;

    ParticleSystem2D m_preview_system{1000};
    SDL_Renderer* m_renderer{nullptr};
    SDL_Texture* m_preview_target{nullptr};
    std::unique_ptr<Renderer2D> m_preview_renderer;
    Camera2D m_preview_camera{480.0f, 270.0f};
    bool m_preview_playing{true};
    float m_preview_accumulator{0.0f};
    float m_preview_elapsed{0.0f};
    float m_preview_delay{0.0f};
    std::string m_error;
};

} // namespace Engine
