#pragma once

#include "Camera2D.hpp"
#include "CameraController2D.hpp"
#include "Scene.hpp"
#include "../ui/UIInputSystem.hpp"
#include "../particles/ParticleWorld.hpp"
#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

class FrameProfiler;

struct RuntimeWorldText {
    std::string text;
    glm::vec2 position{0.0f};
    glm::vec2 velocity{0.0f, 48.0f};
    SDL_Color color{255, 255, 255, 255};
    std::uint8_t base_alpha{255};
    float duration{0.75f};
    float remaining{0.75f};
    float scale{1.0f};
};

class SceneRuntime {
public:
    SceneRuntime();

    void attach(Scene& scene, SDL_Renderer* renderer = nullptr);
    void detach();
    void update(Scene& scene, float delta_time, FrameProfiler* profiler = nullptr);
    void configure_game_view(const RenderViewConfiguration& configuration);

    [[nodiscard]] Camera2D& game_camera() { return m_game_camera; }
    [[nodiscard]] const Camera2D& game_camera() const { return m_game_camera; }
    [[nodiscard]] UIInputSystem& ui_input() { return m_ui_input; }
    [[nodiscard]] const glm::vec2& ui_output_size() const { return m_ui_output_size; }
    [[nodiscard]] const SafeAreaInsets& ui_safe_area() const { return m_ui_safe_area; }
    [[nodiscard]] ParticleWorld& particle_world() { return m_particle_world; }
    [[nodiscard]] const ParticleWorld& particle_world() const { return m_particle_world; }
    bool show_world_text(RuntimeWorldText text);
    [[nodiscard]] const std::vector<RuntimeWorldText>& world_texts() const { return m_world_texts; }

private:
    Camera2D m_game_camera;
    CameraController2D m_camera_controller;
    UIInputSystem m_ui_input;
    ParticleWorld m_particle_world;
    std::vector<RuntimeWorldText> m_world_texts;
    glm::vec2 m_ui_output_size{1920.0f, 1080.0f};
    SafeAreaInsets m_ui_safe_area{};
};

} // namespace Engine
