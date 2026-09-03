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
#include <vector>

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
    void on_context_changed(const EditorPanelContext& context) override {
        set_command_history(context.command_history);
    }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = std::move(ch); }

    void open_asset(AssetHandle handle);
    void on_render_resources_invalidated() override;

    [[nodiscard]] bool owns_draft_history_focus() const;
    [[nodiscard]] bool can_undo_draft() const;
    [[nodiscard]] bool can_redo_draft() const;
    void undo_from_editor_action();
    void redo_from_editor_action();

private:
    enum class PreviewMotion {
        Static,
        FollowCursor,
        FigureEight
    };

    void load_asset(AssetHandle handle);
    [[nodiscard]] bool save_draft();
    void discard_draft();
    void reset_preview(bool play = true);
    void record_draft_edit(const ParticleEffectAsset& before);
    void commit_draft_gesture();
    void render_top_bar(AssetDatabase& database);
    void render_settings(ParticleEffectAsset& asset, AssetDatabase& database);
    void render_preview_workspace(
        const ParticleEffectAsset& asset,
        AssetDatabase& database,
        float height
    );
    void render_pending_change_modal();
    void render_error_modal();
    void destroy_preview();

    std::string m_name{"Particle Effect Editor"};
    std::shared_ptr<EditorCommandHistory> m_command_history;

    AssetHandle m_current_handle{};
    std::optional<ParticleEffectAsset> m_current_asset;
    std::optional<ParticleEffectAsset> m_saved_asset;
    std::string m_current_filepath;
    bool m_is_dirty{false};
    std::optional<ParticleEffectAsset> m_edit_before;
    std::vector<ParticleEffectAsset> m_draft_undo;
    std::vector<ParticleEffectAsset> m_draft_redo;
    std::optional<AssetHandle> m_pending_handle;
    bool m_pending_close{false};
    bool m_has_keyboard_focus{false};
    bool m_error_popup_requested{false};

    ParticleSystem2D m_preview_system{1000};
    SDL_Renderer* m_renderer{nullptr};
    SDL_Texture* m_preview_target{nullptr};
    std::unique_ptr<Renderer2D> m_preview_renderer;
    Camera2D m_preview_camera{480.0f, 270.0f};
    bool m_preview_playing{true};
    bool m_preview_burst_pending{true};
    float m_preview_accumulator{0.0f};
    float m_preview_distance_accumulator{0.0f};
    float m_preview_elapsed{0.0f};
    float m_preview_motion_elapsed{0.0f};
    float m_preview_delay{0.0f};
    PreviewMotion m_preview_motion{PreviewMotion::Static};
    glm::vec2 m_preview_emitter_position{0.0f};
    glm::vec2 m_preview_cursor_position{0.0f};
    glm::vec2 m_preview_previous_emitter_position{0.0f};
    bool m_preview_has_previous_emitter_position{false};
    bool m_preview_cursor_hovered{false};
    SDL_Color m_preview_background{17, 20, 27, 255};
    std::string m_error;
};

} // namespace Engine
