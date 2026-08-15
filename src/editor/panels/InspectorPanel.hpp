#pragma once

#include "../EditorPanel.hpp"
#include "../../ecs/EntityManager.hpp"
#include <string>

namespace Engine {

class SceneManager;
class SceneHierarchyPanel;

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel(SceneManager* scene_manager, SceneHierarchyPanel* hierarchy_panel, struct SDL_Renderer* renderer = nullptr);
    ~InspectorPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_renderer(struct SDL_Renderer* renderer) { m_renderer = renderer; }

private:
    void draw_components(EntityManager& em, EntityID entity);

    std::string m_name{"Entity Inspector"};
    SceneManager* m_scene_manager{nullptr};
    SceneHierarchyPanel* m_hierarchy_panel{nullptr};
    struct SDL_Renderer* m_renderer{nullptr};
};

} // namespace Engine
