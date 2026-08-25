#pragma once

#include "Camera2D.hpp"
#include "CameraController2D.hpp"
#include "Scene.hpp"
#include "../ui/UIInputSystem.hpp"
#include "../particles/ParticleWorld.hpp"

namespace Engine {

class SceneRuntime {
public:
    SceneRuntime();

    void attach(Scene& scene, SDL_Renderer* renderer = nullptr);
    void detach();
    void update(Scene& scene, float delta_time);
    void configure_game_view(const RenderViewConfiguration& configuration);

    [[nodiscard]] Camera2D& game_camera() { return m_game_camera; }
    [[nodiscard]] const Camera2D& game_camera() const { return m_game_camera; }
    [[nodiscard]] UIInputSystem& ui_input() { return m_ui_input; }
    [[nodiscard]] const glm::vec2& ui_output_size() const { return m_ui_output_size; }
    [[nodiscard]] const SafeAreaInsets& ui_safe_area() const { return m_ui_safe_area; }
    [[nodiscard]] ParticleWorld& particle_world() { return m_particle_world; }
    [[nodiscard]] const ParticleWorld& particle_world() const { return m_particle_world; }

private:
    Camera2D m_game_camera;
    CameraController2D m_camera_controller;
    UIInputSystem m_ui_input;
    ParticleWorld m_particle_world;
    glm::vec2 m_ui_output_size{1920.0f, 1080.0f};
    SafeAreaInsets m_ui_safe_area{};
};

} // namespace Engine
