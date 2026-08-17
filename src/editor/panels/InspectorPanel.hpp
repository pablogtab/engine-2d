#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../../ecs/Components.hpp"
#include <imgui.h>
#include <unordered_map>
#include <string>
#include <memory>

namespace Engine {

class SceneManager;

class InspectorPanel : public EditorPanel {
public:
    InspectorPanel(SceneManager* scene_manager, std::shared_ptr<EditorSelection> selection = nullptr, struct SDL_Renderer* renderer = nullptr);
    ~InspectorPanel() override = default;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void set_selection(std::shared_ptr<EditorSelection> selection) { m_selection = selection; }
    void set_renderer(struct SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> command_history) { m_command_history = command_history; }
    void set_animation_studio(class SpriteAnimationStudioPanel* studio) { m_anim_studio = studio; }

private:
    void draw_components(EntityManager& em, EntityID entity);
    void track_transform_edit(EntityManager& em, EntityID entity, const TransformComponent& before, const char* description);
    void push_transform_change(EntityManager& em, EntityID entity, const TransformComponent& before, const char* description);

    std::string m_name{"Entity Inspector"};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    std::unordered_map<ImGuiID, TransformComponent> m_pending_transform_edits;
    struct SDL_Renderer* m_renderer{nullptr};
    class SpriteAnimationStudioPanel* m_anim_studio{nullptr};
};

} // namespace Engine
