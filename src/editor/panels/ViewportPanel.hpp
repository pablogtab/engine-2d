#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../gizmos/TransformGizmo2D.hpp"
#include "../tools/ViewportCameraController.hpp"
#include "../tools/ViewportSelectionTool.hpp"
#include "../tools/TilemapPaintingTool.hpp"
#include "../panels/TilemapPalettePanel.hpp"
#include "../../scene/SceneManager.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>

namespace Engine {

using EditorPlayState = ScenePlayState;

class ViewportPanel : public EditorPanel {
public:
    explicit ViewportPanel(
        SDL_Renderer* renderer, 
        SceneManager* scene_manager = nullptr, 
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::shared_ptr<EditorCommandHistory> command_history = nullptr,
        int internal_width = 1920, 
        int internal_height = 1080
    );
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
    void set_selection(std::shared_ptr<EditorSelection> sel) { m_selection = sel; }
    [[nodiscard]] std::shared_ptr<EditorSelection> get_selection() const { return m_selection; }

    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = ch; }
    [[nodiscard]] std::shared_ptr<EditorCommandHistory> get_command_history() const { return m_command_history; }

    void set_palette_panel(TilemapPalettePanel* palette) { m_palette_panel = palette; }

    int get_internal_width() const { return m_internal_width; }
    int get_internal_height() const { return m_internal_height; }

private:
    std::string m_name{"Scene Viewport"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    TilemapPalettePanel* m_palette_panel{nullptr};
    SDL_Texture* m_viewport_texture{nullptr};

    int m_internal_width{1920};
    int m_internal_height{1080};

    bool m_is_hovered{false};
    bool m_is_focused{false};
    glm::vec2 m_viewport_mouse_pos{0.0f, 0.0f};
    ImVec2 m_transform_toolbar_pos{-1.0f, -1.0f};

    // Sub-tools
    TransformGizmo2D m_gizmo;
    ViewportCameraController m_camera_controller;
    ViewportSelectionTool m_selection_tool;
    TilemapPaintingTool m_tilemap_painting_tool;
};

} // namespace Engine
