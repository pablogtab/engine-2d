#pragma once

#include "../EditorPanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../gizmos/TransformGizmo2D.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>

namespace Engine {

using EditorPlayState = ScenePlayState;
class SceneHierarchyPanel;

class ViewportPanel : public EditorPanel {
public:
    explicit ViewportPanel(SDL_Renderer* renderer, SceneManager* scene_manager = nullptr, int internal_width = 1920, int internal_height = 1080);
    ~ViewportPanel() override;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void begin_render();
    void end_render();

    [[nodiscard]] SDL_Texture* get_viewport_texture() const { return m_viewport_texture; }
    [[nodiscard]] ScenePlayState get_play_state() const { return m_scene_manager ? m_scene_manager->get_play_state() : ScenePlayState::Edit; }

    [[nodiscard]] bool is_viewport_hovered() const { return m_is_hovered; }
    [[nodiscard]] bool is_viewport_focused() const { return m_is_focused; }
    [[nodiscard]] const glm::vec2& get_viewport_mouse_pos() const { return m_viewport_mouse_pos; }

    void set_scene_manager(SceneManager* sm) { m_scene_manager = sm; }
    void set_hierarchy_panel(SceneHierarchyPanel* hp) { m_hierarchy_panel = hp; }

    int get_internal_width() const { return m_internal_width; }
    int get_internal_height() const { return m_internal_height; }

private:
    std::string m_name{"Scene Viewport"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    SceneHierarchyPanel* m_hierarchy_panel{nullptr};
    SDL_Texture* m_viewport_texture{nullptr};

    int m_internal_width{1920};
    int m_internal_height{1080};

    bool m_is_hovered{false};
    bool m_is_focused{false};
    glm::vec2 m_viewport_mouse_pos{0.0f, 0.0f};

    TransformGizmo2D m_gizmo;
    bool m_is_box_selecting{false};
    ImVec2 m_box_select_start{0.0f, 0.0f};
};

} // namespace Engine
