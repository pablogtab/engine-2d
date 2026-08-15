#pragma once

#include "../EditorPanel.hpp"
#include <SDL3/SDL.h>
#include <string>

namespace Engine {

enum class EditorPlayState {
    Edit = 0,
    Play,
    Pause
};

class SceneManager;

class ViewportPanel : public EditorPanel {
public:
    explicit ViewportPanel(SDL_Renderer* renderer, SceneManager* scene_manager = nullptr, int internal_width = 1920, int internal_height = 1080);
    ~ViewportPanel() override;

    void on_gui() override;
    [[nodiscard]] const std::string& get_name() const override { return m_name; }

    void begin_render();
    void end_render();

    [[nodiscard]] SDL_Texture* get_viewport_texture() const { return m_viewport_texture; }
    [[nodiscard]] EditorPlayState get_play_state() const { return m_play_state; }
    void set_play_state(EditorPlayState state) { m_play_state = state; }

    [[nodiscard]] bool is_viewport_hovered() const { return m_is_hovered; }
    [[nodiscard]] bool is_viewport_focused() const { return m_is_focused; }

    void set_scene_manager(SceneManager* sm) { m_scene_manager = sm; }

    int get_internal_width() const { return m_internal_width; }
    int get_internal_height() const { return m_internal_height; }

private:
    std::string m_name{"Scene Viewport"};
    SDL_Renderer* m_renderer{nullptr};
    SceneManager* m_scene_manager{nullptr};
    SDL_Texture* m_viewport_texture{nullptr};

    int m_internal_width{1920};
    int m_internal_height{1080};

    EditorPlayState m_play_state{EditorPlayState::Play};
    bool m_is_hovered{false};
    bool m_is_focused{false};
};

} // namespace Engine
