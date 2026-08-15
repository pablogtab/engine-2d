#pragma once

#include "../EditorPanel.hpp"
#include "../../ecs/EntityManager.hpp"
#include <string>

namespace Engine {

class SceneManager;

class SceneHierarchyPanel : public EditorPanel {
public:
    SceneHierarchyPanel(SceneManager* scene_manager);
    ~SceneHierarchyPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    [[nodiscard]] EntityID get_selected_entity() const { return m_selected_entity; }
    void set_selected_entity(EntityID id) { m_selected_entity = id; }

private:
    std::string m_name{"Scene Hierarchy"};
    SceneManager* m_scene_manager{nullptr};
    EntityID m_selected_entity{0};
    char m_search_filter[64]{""};
};

} // namespace Engine
