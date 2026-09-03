#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../../scene/SceneValidator.hpp"
#include <memory>
#include <string>
#include <functional>
#include <optional>

namespace Engine {

class Scene;
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
    void on_context_changed(const EditorPanelContext& context) override {
        set_command_history(context.command_history);
        set_selection(context.selection);
    }
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = std::move(selection); }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) { m_command_history = std::move(command_history); }
    void set_on_reveal_asset(std::function<void(const std::string&)> callback) { m_on_reveal_asset = std::move(callback); }

private:
    void draw_diagnostic_row(
        Scene& scene,
        const SceneDiagnostic& diagnostic,
        std::size_t index
    );

    std::string m_name{"Scene Diagnostics"};
    SceneManager* m_scene_manager{nullptr};
    AssetDatabase* m_asset_database{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
    std::shared_ptr<EditorCommandHistory> m_command_history;
    std::function<void(const std::string&)> m_on_reveal_asset;
    std::optional<SceneDiagnostic> m_pending_clear;
    char m_filter_buffer[96]{};
    bool m_show_errors{true};
    bool m_show_warnings{true};
};

} // namespace Engine
