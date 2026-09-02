#pragma once

#include "../EditorPanel.hpp"
#include "../../core/AssetHandle.hpp"
#include "../../renderer/postprocess/PostProcessProfile.hpp"
#include <memory>
#include <optional>
#include <string>

namespace Engine {

class EditorCommandHistory;
class PostProcessPipeline;
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
    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = std::move(ch); }

private:
    void apply_immediate(
        PostProcessProfile* profile,
        PostProcessPipeline* pipeline,
        PostProcessProfile next
    );
    void track_property_interaction(
        PostProcessProfile* profile,
        PostProcessPipeline* pipeline,
        const PostProcessProfile& before,
        bool changed
    );

    std::string m_name{"Post-Processing Tuner"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history;
    PostProcessProfile* m_interaction_target{nullptr};
    std::optional<PostProcessProfile> m_interaction_before;
    AssetHandle m_selected_profile_handle{};
};

} // namespace Engine
