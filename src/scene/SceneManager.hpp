#pragma once

#include "Scene.hpp"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <SDL3/SDL.h>

namespace Engine {

enum class ScenePlayState {
    Edit,
    Play,
    Pause
};

class SceneManager {
public:
    SceneManager() = default;

    void add_scene(std::shared_ptr<Scene> scene);
    void set_active_scene(size_t index);
    void set_active_scene(const std::string& name);
    void next_scene();
    void previous_scene();

    void restore_state();

    // Two-Phase Play Mode lifecycle
    void start_play();
    void pause_play();
    void resume_play();
    void stop_play();

    [[nodiscard]] ScenePlayState get_play_state() const { return m_play_state; }
    [[nodiscard]] bool is_playing() const { return m_play_state == ScenePlayState::Play; }
    [[nodiscard]] bool is_paused() const { return m_play_state == ScenePlayState::Pause; }
    [[nodiscard]] bool is_editing() const { return m_play_state == ScenePlayState::Edit; }

    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_on_authoring_state_invalidated(std::function<void()> callback) { m_on_authoring_state_invalidated = std::move(callback); }

    void update(float delta_time);
    void render(Renderer2D& renderer);
    void resize(int width, int height, float dpi_scale = 1.0f);

    Scene* get_active_scene() const;
    size_t get_scene_count() const { return m_scenes.size(); }
    int get_active_index() const { return m_active_index; }
    [[nodiscard]] const std::vector<std::shared_ptr<Scene>>& get_scenes() const { return m_scenes; }

private:
    void save_state();

    std::vector<std::shared_ptr<Scene>> m_scenes;
    int m_active_index{-1};
    int m_last_width{1280};
    int m_last_height{720};
    float m_last_dpi_scale{1.0f};

    ScenePlayState m_play_state{ScenePlayState::Edit};
    std::string m_snapshot_json;
    SDL_Renderer* m_renderer{nullptr};
    std::function<void()> m_on_authoring_state_invalidated;
};

} // namespace Engine
