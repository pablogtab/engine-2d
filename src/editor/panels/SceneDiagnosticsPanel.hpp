#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include <memory>
#include <string>
#include <functional>

namespace Engine {

class SceneManager;
class AssetDatabase;

class SceneDiagnosticsPanel final : public EditorPanel {
public:
    SceneDiagnosticsPanel(
        SceneManager* scene_manager,
        AssetDatabase* asset_database,
        std::shared_ptr<EditorSelection> selection
    );

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = std::move(selection); }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) { m_command_history = std::move(command_history); }
    void set_on_reveal_asset(std::function<void(const std::string&)> callback) { m_on_reveal_asset = std::move(callback); }

private:
    std::string m_name{"Scene Diagnostics"};
    SceneManager* m_scene_manager{nullptr};
    AssetDatabase* m_asset_database{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::function<void(const std::string&)> m_on_reveal_asset;
};

} // namespace Engine
