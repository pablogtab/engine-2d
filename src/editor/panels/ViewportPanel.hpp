#pragma once

#include "../EditorPanel.hpp"
#include "../EditorSelection.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../gizmos/TransformGizmo2D.hpp"
#include "../gizmos/UILayoutGizmo.hpp"
#include "../tools/ViewportCameraController.hpp"
#include "../tools/ViewportArrangementTool.hpp"
#include "../tools/ViewportSelectionTool.hpp"
#include "../tools/ViewportReferenceOverlay.hpp"
#include "../tools/ViewportColliderOverlay.hpp"
#include "../tools/ViewportLightingOverlay.hpp"
#include "../tools/EditorEntityVisibilityFilter.hpp"
#include "../tools/EditorToolbox.hpp"
#include "../tools/DevicePreview.hpp"
#include "../tools/TilemapPaintingTool.hpp"
#include "../panels/TilemapPalettePanel.hpp"
#include "../stage/PrefabStageController.hpp"
#include "../../scene/SceneManager.hpp"
#include "../../scene/Camera2D.hpp"
#include "../../core/ProjectSettings.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <string>
#include <memory>
#include <array>
#include <cstdint>
#include <vector>

namespace Engine {

using EditorPlayState = ScenePlayState;
class Renderer2D;
class EntityManager;

struct ViewportGridSettings {
    bool visible{true};
    float target_spacing_pixels{38.0f};
    int major_line_interval{5};
    float minor_opacity{0.10f};
    float major_opacity{0.23f};
    bool show_axes{true};
};

struct ViewportAiState {
    glm::vec2 center{0.0f, 0.0f};
    glm::vec2 world_min{0.0f, 0.0f};
    glm::vec2 world_max{0.0f, 0.0f};
    float zoom{1.0f};
    int render_width{0};
    int render_height{0};
    std::uint64_t render_generation{0};
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
    void on_render_resources_invalidated() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void begin_render();
    void end_render();

    [[nodiscard]] SDL_Texture* get_viewport_texture() const { return m_viewport_texture; }
    [[nodiscard]] ScenePlayState get_play_state() const { return m_scene_manager ? m_scene_manager->get_play_state() : ScenePlayState::Edit; }

    [[nodiscard]] bool is_viewport_hovered() const { return m_is_hovered; }
    [[nodiscard]] bool is_viewport_focused() const { return m_is_focused; }
    [[nodiscard]] const glm::vec2& get_viewport_mouse_pos() const { return m_viewport_mouse_pos; }

    void set_scene_manager(SceneManager* sm) { m_scene_manager = sm; }
    void set_engine_renderer(Renderer2D* renderer) { m_engine_renderer = renderer; }
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
    void set_safe_area_preview(const SafeAreaInsets& insets) { m_safe_area_preview = insets; }
    [[nodiscard]] bool show_safe_area_overlay() const { return m_show_safe_area_overlay; }

    [[nodiscard]] bool get_ai_view_state(ViewportAiState& state, std::string& error) const;
    [[nodiscard]] bool ai_pan(const glm::vec2& world_delta, std::string& error);
    [[nodiscard]] bool ai_zoom(float factor, std::string& error);
    [[nodiscard]] bool ai_focus_entities(
        const std::vector<EntityID>& entities,
        float padding,
        std::string& error
    );
    [[nodiscard]] bool ai_frame_all(float padding, std::string& error);
    [[nodiscard]] bool ai_reset(std::string& error);
    [[nodiscard]] bool capture_viewport_png(
        const std::string& path,
        std::size_t& byte_count,
        std::string& error
    );

    // View menu entry point. Grid preferences are view-only session state.
    void render_grid_settings_menu();
    void render_arrange_menu();

    void set_on_load_scene_callback(std::function<void(const std::string&)> callback) {
        m_on_load_scene = std::move(callback);
    }
    void set_on_activate_scene_callback(std::function<void(std::size_t)> callback) {
        m_on_activate_scene = std::move(callback);
    }
    void set_is_scene_dirty_callback(std::function<bool(const Scene*)> callback) {
        m_is_scene_dirty = std::move(callback);
    }
    void set_is_scene_tab_open_callback(std::function<bool(const Scene*)> callback) {
        m_is_scene_tab_open = std::move(callback);
    }

    void set_prefab_stage(PrefabStageController* stage) { m_prefab_stage = stage; }
    // Called by breadcrumb UI to request back-to-scene navigation (may show a dirty modal)
    void set_on_close_prefab_stage_callback(std::function<void()> callback) {
        m_on_close_prefab_stage = std::move(callback);
    }
    void set_on_save_prefab_callback(std::function<void()> callback) {
        m_on_save_prefab = std::move(callback);
    }

private:
    std::function<void(const std::string&)> m_on_load_scene;
    std::function<void(std::size_t)> m_on_activate_scene;
    std::function<bool(const Scene*)> m_is_scene_dirty;
    std::function<bool(const Scene*)> m_is_scene_tab_open;
    std::function<void()> m_on_close_prefab_stage;
    std::function<void()> m_on_save_prefab;
    PrefabStageController* m_prefab_stage{nullptr};
    std::string m_name{"Scene Viewport"};
    SDL_Renderer* m_renderer{nullptr};
    Renderer2D* m_engine_renderer{nullptr};
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
    std::uint64_t m_render_generation{0};

    bool m_is_hovered{false};
    bool m_is_focused{false};
    glm::vec2 m_viewport_mouse_pos{0.0f, 0.0f};
    EditorToolbox m_transform_toolbox;
    ViewportGridSettings m_grid_settings;
    SafeAreaInsets m_safe_area_preview{};
    bool m_show_safe_area_overlay{true};
    std::unique_ptr<EntityManager> m_prefab_preview_entities;
    std::string m_prefab_preview_path;
    EntityID m_prefab_preview_root{0};
    bool m_prefab_preview_visible{false};
    glm::vec2 m_prefab_preview_position{0.0f, 0.0f};
    std::size_t m_device_preview_profile{0};
    DevicePreviewOrientation m_device_preview_orientation{DevicePreviewOrientation::Portrait};
    DevicePreviewProfile m_custom_device_preview{
        "Custom", 390, 844, {0.0f, 47.0f, 0.0f, 34.0f}
    };
    SafeAreaInsets m_game_safe_area_render{};
    ViewportReferenceOverlay m_reference_overlay;
    ViewportColliderOverlay m_collider_overlay;
    ViewportLightingOverlay m_lighting_overlay;
    EntityID m_context_entity{0};
    std::array<char, 128> m_context_rename_buffer{};

    void render_entity_context_menu(EntityManager& em, EntityID entity);
    void render_rename_entity_popup(EntityManager& em);
    void render_transform_toolbar();
    void render_device_preview_toolbar();
    bool ensure_render_target();
    void configure_scene_view();
    [[nodiscard]] bool has_device_preview() const;
    [[nodiscard]] DevicePreviewProfile active_device_preview() const;
    [[nodiscard]] bool is_position_snap_active() const;
    bool prepare_prefab_preview(const std::string& path, const glm::vec2& world_position);
    void render_prefab_preview(
        const Camera2D& camera,
        const ImVec2& image_min,
        const ImVec2& image_size
    );

    // Sub-tools
    TransformGizmo2D m_gizmo;
    UILayoutGizmo m_ui_layout_gizmo;
    ViewportCameraController m_camera_controller;
    ViewportSelectionTool m_selection_tool;
    TilemapPaintingTool m_tilemap_painting_tool;
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store{nullptr};
    SceneViewEntityFilter m_visibility_filter;
};

} // namespace Engine
