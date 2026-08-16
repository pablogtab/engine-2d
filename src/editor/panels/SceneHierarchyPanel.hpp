#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include <string>
#include <memory>

namespace Engine {

class SceneManager;
class EntityManager;

class SceneHierarchyPanel : public EditorPanel {
public:
    SceneHierarchyPanel(
        SceneManager* scene_manager, 
        struct SDL_Renderer* renderer = nullptr, 
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::shared_ptr<EditorCommandHistory> command_history = nullptr
    );
    ~SceneHierarchyPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = selection; }
    [[nodiscard]] std::shared_ptr<EditorSelection> get_selection() const { return m_selection; }

    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = ch; }
    [[nodiscard]] std::shared_ptr<EditorCommandHistory> get_command_history() const { return m_command_history; }

    void set_renderer(struct SDL_Renderer* renderer) { m_renderer = renderer; }

    // Legacy helper compatibility
    [[nodiscard]] EntityID get_selected_entity() const { 
        return m_selection ? m_selection->get_primary() : 0; 
    }

private:
    void draw_entity_node(EntityManager& em, EntityID id, const std::string& filter_str, bool is_multi_key);
    bool matches_filter_recursive(EntityManager& em, EntityID id, const std::string& filter_str) const;
    bool is_descendant(EntityManager& em, EntityID potential_descendant, EntityID ancestor) const;
    void create_entity_helper(EntityManager& em, EntityID parent_id = 0);
    void create_tilemap_helper(EntityManager& em, EntityID parent_id = 0);

    std::string m_name{"Scene Hierarchy"};
    SceneManager* m_scene_manager{nullptr};
    struct SDL_Renderer* m_renderer{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    char m_search_filter[64]{""};
};

} // namespace Engine
