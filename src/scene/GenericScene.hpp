#pragma once

#include "Scene.hpp"
#include "Camera2D.hpp"
#include "CameraController2D.hpp"
#include "../renderer/Renderer2D.hpp"

namespace Engine {

class GenericScene : public Scene {
public:
    explicit GenericScene(std::string name = "Untitled Scene", float width = 1920.0f, float height = 1080.0f);
    ~GenericScene() override = default;

    void on_attach() override;
    void on_detach() override;
    void on_update(float delta_time) override;
    void on_render(Renderer2D& renderer) override;
    void on_resize(int width, int height, float dpi_scale = 1.0f) override;

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
    void focus_editor_camera(const glm::vec2& target_pos);

    void set_physics_enabled(bool enabled) { m_physics_enabled = enabled; }
    [[nodiscard]] bool is_physics_enabled() const { return m_physics_enabled; }

    void set_debug_colliders(bool show) { m_show_debug_colliders = show; }
    [[nodiscard]] bool is_debug_colliders() const { return m_show_debug_colliders; }

    void set_is_editing(bool is_editing) override { m_is_editing = is_editing; }
    [[nodiscard]] bool is_editing() const override { return m_is_editing; }

private:
    Camera2D m_camera;
    Camera2D m_editor_camera;
    CameraController2D m_camera_controller;
    bool m_physics_enabled{true};
    bool m_show_debug_colliders{false};
    bool m_is_editing{true};
    int m_viewport_w{1920};
    int m_viewport_h{1080};
    float m_dpi_scale{1.0f};
};

} // namespace Engine
