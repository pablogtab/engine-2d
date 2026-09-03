#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Engine {

class AssetDatabase;
class EditorCommandHistory;
class PostProcessPipeline;
class Scene;
class SceneManager;

class PostProcessTunerPanel : public EditorPanel {
public:
    PostProcessTunerPanel(
        SceneManager* scene_manager,
        std::shared_ptr<EditorCommandHistory> command_history
    );
    ~PostProcessTunerPanel() override = default;

    void on_gui() override;
    void on_context_changed(const EditorPanelContext& context) override {
        set_command_history(context.command_history);
    }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) {
        m_command_history = std::move(command_history);
    }

    [[nodiscard]] bool owns_draft_history_focus() const;
    [[nodiscard]] bool can_undo_draft() const;
    [[nodiscard]] bool can_redo_draft() const;
    void undo_from_editor_action();
    void redo_from_editor_action();

private:
    void observe_target(
        Scene* scene,
        PostProcessProfile* profile,
        PostProcessPipeline* pipeline,
        AssetHandle handle,
        AssetDatabase& database
    );
    void bind_observed_target(AssetDatabase& database);
    [[nodiscard]] bool observed_target_is_bound() const;
    [[nodiscard]] bool observed_target_is_deferred() const;
    void apply_preview();
    void apply_runtime_profile(AssetHandle handle, const PostProcessProfile& profile);

    void record_draft_edit(const PostProcessProfile& before);
    void commit_draft_gesture();
    [[nodiscard]] bool save_draft();
    void discard_draft();
    void accept_saved_asset(
        const std::string& filepath,
        AssetHandle handle,
        const PostProcessProfile& profile
    );

    void render_top_bar(AssetDatabase& database);
    void render_settings(PostProcessProfile& profile, PostProcessPipeline* pipeline);
    void render_pending_change_modal(AssetDatabase& database);
    void render_error_modal();

    std::string m_name{"Post-Processing Tuner"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history;

    Scene* m_bound_scene{nullptr};
    PostProcessProfile* m_bound_profile{nullptr};
    PostProcessPipeline* m_bound_pipeline{nullptr};
    AssetHandle m_bound_handle{};
    std::string m_bound_filepath;

    Scene* m_observed_scene{nullptr};
    PostProcessProfile* m_observed_profile{nullptr};
    PostProcessPipeline* m_observed_pipeline{nullptr};
    AssetHandle m_observed_handle{};

    Scene* m_deferred_scene{nullptr};
    PostProcessProfile* m_deferred_profile{nullptr};
    AssetHandle m_deferred_handle{};

    std::optional<PostProcessProfile> m_saved_profile;
    std::optional<PostProcessProfile> m_draft_profile;
    std::optional<PostProcessProfile> m_edit_before;
    std::vector<PostProcessProfile> m_draft_undo;
    std::vector<PostProcessProfile> m_draft_redo;

    bool m_is_dirty{false};
    bool m_preview_draft{true};
    bool m_has_keyboard_focus{false};
    bool m_pending_close{false};
    bool m_pending_target_change{false};
    bool m_error_popup_requested{false};
    std::string m_error;
};

} // namespace Engine
