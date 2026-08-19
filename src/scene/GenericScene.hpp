#pragma once

#include "Scene.hpp"
#include "Camera2D.hpp"
#include "CameraController2D.hpp"
#include "../renderer/Renderer2D.hpp"
#include <vector>

namespace Engine {

class GenericScene : public Scene {
public:
    static constexpr float EDITOR_MIN_ZOOM{Camera2D::MIN_ZOOM};
    static constexpr float EDITOR_MAX_ZOOM{Camera2D::MAX_ZOOM};

    explicit GenericScene(std::string name = "Untitled Scene", float width = 1920.0f, float height = 1080.0f);
    ~GenericScene() override = default;

    void on_attach() override;
    void on_detach() override;
    void on_update(float delta_time) override;
    void on_render(Renderer2D& renderer) override;
    void on_viewport_resize(RenderViewKind view, const RenderViewConfiguration& configuration) override;

    // Camera Access
    [[nodiscard]] Camera2D& get_camera() { return m_camera; }
    [[nodiscard]] const Camera2D& get_camera() const { return m_camera; }
    [[nodiscard]] CameraController2D& get_camera_controller() { return m_camera_controller; }

    [[nodiscard]] Camera2D& get_editor_camera() { return m_editor_camera; }
    [[nodiscard]] const Camera2D& get_editor_camera() const { return m_editor_camera; }
    [[nodiscard]] Camera2D& get_active_camera(bool is_editing) { return is_editing ? m_editor_camera : m_camera; }

    // Editor Camera Manipulation
    void pan_editor_camera(const glm::vec2& delta_pixels);
    void zoom_editor_camera(float zoom_factor);
    void zoom_editor_camera_at(const glm::vec2& viewport_position, float zoom_factor);
    void focus_editor_camera(const glm::vec2& target_pos);
    void frame_editor_camera(const glm::vec2& bounds_min, const glm::vec2& bounds_max, float padding = 0.15f);
    void reset_editor_camera();

    void set_physics_enabled(bool enabled) { m_physics_enabled = enabled; }
    [[nodiscard]] bool is_physics_enabled() const { return m_physics_enabled; }

    void set_debug_colliders(bool show) { m_show_debug_colliders = show; }
    [[nodiscard]] bool is_debug_colliders() const { return m_show_debug_colliders; }

    void set_is_editing(bool is_editing) override { m_is_editing = is_editing; }
    [[nodiscard]] bool is_editing() const override { return m_is_editing; }

    void set_render_filter(const class IEntityRenderFilter* filter) { m_render_filter = filter; }
    [[nodiscard]] const class IEntityRenderFilter* get_render_filter() const { return m_render_filter; }

private:
    void synchronize_game_camera(float delta_time, bool reset_controller);

    Camera2D m_camera;
    Camera2D m_editor_camera;
    CameraController2D m_camera_controller;
    EntityID m_active_camera_entity{0};
    const class IEntityRenderFilter* m_render_filter{nullptr};
    bool m_physics_enabled{true};
    bool m_show_debug_colliders{false};
    bool m_is_editing{true};
    std::vector<EntityID> m_pending_coin_collections;
};

} // namespace Engine
