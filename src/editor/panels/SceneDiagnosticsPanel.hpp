#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include <memory>
#include <string>

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

private:
    std::string m_name{"Scene Diagnostics"};
    SceneManager* m_scene_manager{nullptr};
    AssetDatabase* m_asset_database{nullptr};
    std::shared_ptr<EditorSelection> m_selection;
};

} // namespace Engine
