#pragma once

#include "Scene.hpp"
#include "Camera2D.hpp"
#include "CameraController2D.hpp"
#include "../renderer/Renderer2D.hpp"
#include "../renderer/postprocess/PostProcessPipeline.hpp"
#include "../renderer/postprocess/PostProcessProfile.hpp"
#include "../renderer/RenderEffectSystem.hpp"
#include "../renderer/Lighting2DSystem.hpp"
#include "../ui/CanvasUISystem.hpp"
#include "../ui/UIInputSystem.hpp"
#include <vector>

namespace Engine {

class GenericScene : public Scene {
public:
    explicit GenericScene(std::string name = "Untitled Scene", float width = 1920.0f, float height = 1080.0f);
    ~GenericScene() override = default;

    void on_attach() override;
    void on_detach() override;
    void on_update(float delta_time) override;
    void on_render(Renderer2D& renderer) override;
    void on_viewport_resize(RenderViewKind view, const RenderViewConfiguration& configuration) override;
    void on_render_resources_invalidated() override;
    [[nodiscard]] SDL_Color get_clear_color() const override;
    [[nodiscard]] PostProcessPipeline* get_post_process_pipeline() override { return m_post_process_enabled ? &m_post_process_pipeline : nullptr; }
    [[nodiscard]] PostProcessProfile* get_editable_post_process_profile() override { return m_post_process_enabled ? &m_post_process_profile : nullptr; }
    [[nodiscard]] bool supports_post_process_profile_assets() const override { return true; }
    [[nodiscard]] AssetHandle get_post_process_profile_handle() const override { return m_post_process_profile_handle; }
    bool set_post_process_profile_handle(AssetHandle handle, const AssetDatabase& database) override;
    [[nodiscard]] bool supports_render_layer_effects() const override { return true; }
    [[nodiscard]] const std::vector<RenderLayerEffectAssignment>& get_render_layer_effect_assignments() const override {
        return m_render_effect_system.layer_assignments();
    }
    bool set_render_layer_effect_assignments(std::vector<RenderLayerEffectAssignment> assignments, const AssetDatabase& database) override;
    bool refresh_render_layer_effects(const AssetDatabase& database) override;

    [[nodiscard]] Camera2D* get_render_camera(RenderViewKind view) override {
        return view == RenderViewKind::Editor ? &m_editor_camera : &m_camera;
    }
    [[nodiscard]] const Camera2D* get_render_camera(RenderViewKind view) const override {
        return view == RenderViewKind::Editor ? &m_editor_camera : &m_camera;
    }
    [[nodiscard]] CameraController2D& get_camera_controller() { return m_camera_controller; }

    void set_physics_enabled(bool enabled) { m_physics_enabled = enabled; }
    [[nodiscard]] bool is_physics_enabled() const { return m_physics_enabled; }

    void set_debug_colliders(bool show) { m_show_debug_colliders = show; }
    [[nodiscard]] bool is_debug_colliders() const { return m_show_debug_colliders; }

    void set_is_editing(bool is_editing) override { m_is_editing = is_editing; }
    [[nodiscard]] bool is_editing() const override { return m_is_editing; }

    void set_render_filter(const IEntityRenderFilter* filter) override { m_render_filter = filter; }
    [[nodiscard]] const class IEntityRenderFilter* get_render_filter() const { return m_render_filter; }

private:
    void synchronize_game_camera(float delta_time, bool reset_controller);
    void initialize_post_process(Renderer2D& renderer);

    Camera2D m_camera;
    Camera2D m_editor_camera;
    CameraController2D m_camera_controller;
    EntityID m_active_camera_entity{0};
    const IEntityRenderFilter* m_render_filter{nullptr};
    bool m_physics_enabled{true};
    bool m_show_debug_colliders{false};
    bool m_is_editing{true};
    PostProcessPipeline m_post_process_pipeline;
    PostProcessProfile m_post_process_profile;
    AssetHandle m_post_process_profile_handle{};
    RenderEffectSystem m_render_effect_system;
    Lighting2DSystem m_lighting_system;
    CanvasUISystem m_canvas_ui_system;
    UIInputSystem m_ui_input_system;
    glm::vec2 m_ui_output_size{1920.0f, 1080.0f};
    SafeAreaInsets m_ui_safe_area{};
    SafeAreaInsets m_ui_editor_safe_area{};
    bool m_show_ui_safe_area_overlay{false};
    bool m_post_process_enabled{false};
    bool m_post_process_initialized{false};
};

} // namespace Engine
