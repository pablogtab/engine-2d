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
    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = std::move(ch); }

private:
    enum class Category {
        Display,
        Rendering,
        Window
    };

    void apply_changes();
    void draw_navigation();
    void draw_display_settings();
    void draw_rendering_settings();
    void draw_window_settings();
    void draw_action_bar(const ProjectDisplaySettings& current);

    std::string m_name{"Project Settings"};
    ProjectSettings* m_settings{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history;
    ProjectDisplaySettings m_pending;
    ProjectDisplaySettings m_observed;
    Category m_category{Category::Display};
    bool m_initialized{false};
};

} // namespace Engine
