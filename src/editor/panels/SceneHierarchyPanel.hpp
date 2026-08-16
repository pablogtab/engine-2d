#pragma once

#include "../EditorPanel.hpp"
#include "../../ecs/EntityManager.hpp"
#include <string>
#include <unordered_set>

namespace Engine {

class SceneManager;

class SceneHierarchyPanel : public EditorPanel {
public:
    SceneHierarchyPanel(SceneManager* scene_manager, struct SDL_Renderer* renderer = nullptr);
    ~SceneHierarchyPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    [[nodiscard]] EntityID get_selected_entity() const { 
        return m_selected_entities.empty() ? 0 : *m_selected_entities.begin(); 
    }
    [[nodiscard]] const std::unordered_set<EntityID>& get_selected_entities() const { return m_selected_entities; }
    [[nodiscard]] bool is_selected(EntityID id) const { return m_selected_entities.contains(id); }

    void set_selected_entity(EntityID id) { 
        m_selected_entities.clear(); 
        if (id != 0) m_selected_entities.insert(id); 
    }
    void select_entity(EntityID id, bool additive = false) {
        if (!additive) {
            m_selected_entities.clear();
            if (id != 0) m_selected_entities.insert(id);
        } else {
            if (m_selected_entities.contains(id)) {
                m_selected_entities.erase(id);
            } else if (id != 0) {
                m_selected_entities.insert(id);
            }
        }
    }
    void set_selected_entities(const std::unordered_set<EntityID>& entities) {
        m_selected_entities = entities;
    }
    void clear_selection() { m_selected_entities.clear(); }

    void set_renderer(struct SDL_Renderer* renderer) { m_renderer = renderer; }

private:
    std::string m_name{"Scene Hierarchy"};
    SceneManager* m_scene_manager{nullptr};
    struct SDL_Renderer* m_renderer{nullptr};
    std::unordered_set<EntityID> m_selected_entities;
    char m_search_filter[64]{""};
};

} // namespace Engine
