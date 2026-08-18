#pragma once

#include "../EditorPanel.hpp"
#include "../../core/ProjectSettings.hpp"
#include <memory>
#include <string>

namespace Engine {

class EditorCommandHistory;
class SceneManager;

class ProjectSettingsPanel final : public EditorPanel {
public:
    ProjectSettingsPanel(
        ProjectSettings* settings,
        SceneManager* scene_manager,
        std::shared_ptr<EditorCommandHistory> command_history
    );

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    void apply_changes();

    std::string m_name{"Project Settings"};
    ProjectSettings* m_settings{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history;
    GameViewSettings m_pending;
    GameViewSettings m_observed;
    bool m_initialized{false};
};

} // namespace Engine
