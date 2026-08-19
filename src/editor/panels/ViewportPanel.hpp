#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../gizmos/TransformGizmo2D.hpp"
#include "../tools/ViewportCameraController.hpp"
#include "../tools/ViewportArrangementTool.hpp"
#include "../tools/ViewportSelectionTool.hpp"
#include "../tools/ViewportReferenceOverlay.hpp"
#include "../tools/ViewportColliderOverlay.hpp"
#include "../tools/EditorEntityVisibilityFilter.hpp"
#include "../tools/EditorToolbox.hpp"
#include "../tools/TilemapPaintingTool.hpp"
#include "../panels/TilemapPalettePanel.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Camera2D.hpp"
#include "../../core/ProjectSettings.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <array>

namespace Engine {

using EditorPlayState = ScenePlayState;

struct ViewportGridSettings {
    bool visible{true};
    float target_spacing_pixels{38.0f};
    int major_line_interval{5};
    float minor_opacity{0.10f};
    float major_opacity{0.23f};
    bool show_axes{true};
};


class ViewportPanel : public EditorPanel {
public:
    explicit ViewportPanel(
        SDL_Renderer* renderer, 
        SceneManager* scene_manager = nullptr, 
        std::shared_ptr<EditorSelection> selection = nullptr,
        std::shared_ptr<EditorCommandHistory> command_history = nullptr,
        ProjectSettings* project_settings = nullptr
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

    void set_entity_state_store(std::shared_ptr<EditorEntityStateStore> store) {
        m_entity_state_store = store;
        m_visibility_filter.set_store(m_entity_state_store.get());
    }
    [[nodiscard]] std::shared_ptr<EditorEntityStateStore> get_entity_state_store() const { return m_entity_state_store; }
    [[nodiscard]] const SceneViewEntityFilter& get_visibility_filter() const { return m_visibility_filter; }

    void set_palette_panel(TilemapPalettePanel* palette) { m_palette_panel = palette; }

    int get_render_width() const { return m_render_width; }
    int get_render_height() const { return m_render_height; }

    // View menu entry point. Grid preferences are view-only session state.
    void render_grid_settings_menu();
    void render_arrange_menu();

    void set_on_load_scene_callback(std::function<void(const std::string&)> callback) {
        m_on_load_scene = std::move(callback);
    }

private:
    std::function<void(const std::string&)> m_on_load_scene;
    std::string m_name{"Scene Viewport"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    ProjectSettings* m_project_settings{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    TilemapPalettePanel* m_palette_panel{nullptr};
    SDL_Texture* m_viewport_texture{nullptr};

    int m_render_width{1280};
    int m_render_height{720};
    int m_requested_width{1280};
    int m_requested_height{720};
    int m_editor_view_width{1280};
    int m_editor_view_height{720};
    int m_game_preview_width{960};
    int m_game_preview_height{540};

    bool m_is_hovered{false};
    bool m_is_focused{false};
    glm::vec2 m_viewport_mouse_pos{0.0f, 0.0f};
    EditorToolbox m_transform_toolbox;
    ViewportGridSettings m_grid_settings;
    ViewportReferenceOverlay m_reference_overlay;
    ViewportColliderOverlay m_collider_overlay;
    EntityID m_context_entity{0};
    std::array<char, 128> m_context_rename_buffer{};

    void render_entity_context_menu(EntityManager& em, EntityID entity);
    void render_rename_entity_popup(EntityManager& em);
    void render_transform_toolbar();
    bool ensure_render_target();
    void configure_scene_view();

    // Sub-tools
    TransformGizmo2D m_gizmo;
    ViewportCameraController m_camera_controller;
    ViewportSelectionTool m_selection_tool;
    TilemapPaintingTool m_tilemap_painting_tool;
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store{nullptr};
    SceneViewEntityFilter m_visibility_filter;
};

} // namespace Engine
