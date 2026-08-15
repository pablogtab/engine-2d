#pragma once

#include "../EditorPanel.hpp"
#include "../../ecs/EntityManager.hpp"
#include <string>

namespace Engine {

class SceneManager;
class SceneHierarchyPanel;

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel(SceneManager* scene_manager, SceneHierarchyPanel* hierarchy_panel);
    ~InspectorPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

private:
    void draw_components(EntityManager& em, EntityID entity);

    std::string m_name{"Entity Inspector"};
    SceneManager* m_scene_manager{nullptr};
    SceneHierarchyPanel* m_hierarchy_panel{nullptr};
};

} // namespace Engine
