#pragma once

#include "Scene.hpp"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <SDL3/SDL.h>

namespace Engine {

class AssetDatabase;
struct MaterialAssetRefreshResult;

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

    // Transient scene: active overlay that is NOT part of m_scenes.
    // Used by PrefabStageController. Does not affect .scene_state, demo navigation, or Play.
    void enter_transient_scene(std::shared_ptr<Scene> stage);
    void exit_transient_scene();
    [[nodiscard]] bool is_transient_scene_active() const { return m_transient_scene != nullptr; }
    [[nodiscard]] std::shared_ptr<Scene> get_transient_scene() const { return m_transient_scene; }

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
    void configure_view(RenderViewKind view, const RenderViewConfiguration& configuration);
    [[nodiscard]] MaterialAssetRefreshResult refresh_material_assets(const AssetDatabase& database);

    Scene* get_active_scene() const;
    [[nodiscard]] std::shared_ptr<Scene> get_active_scene_shared_ptr() const;
    size_t get_scene_count() const { return m_scenes.size(); }
    int get_active_index() const { return m_active_index; }
    [[nodiscard]] const std::vector<std::shared_ptr<Scene>>& get_scenes() const { return m_scenes; }

private:
    void save_state();
    void apply_active_view();

    std::vector<std::shared_ptr<Scene>> m_scenes;
    int m_active_index{-1};
    std::shared_ptr<Scene> m_transient_scene{nullptr};
    RenderViewConfiguration m_editor_view;
    RenderViewConfiguration m_game_view;

    ScenePlayState m_play_state{ScenePlayState::Edit};
    std::string m_snapshot_json;
    SDL_Renderer* m_renderer{nullptr};
    std::function<void()> m_on_authoring_state_invalidated;
};

} // namespace Engine
