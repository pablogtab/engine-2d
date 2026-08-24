#pragma once

#include "Scene.hpp"
#include "../scripting/ScriptRuntime.hpp"
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <SDL3/SDL.h>

namespace Engine {

class AssetDatabase;
class Material;
struct MaterialAssetRefreshResult;

enum class ScenePlayState {
    Edit,
    Play,
    Pause
};

struct SceneLoadProgress {
    bool active{false};
    float value{0.0f};
    std::string label;
    std::string detail;
};

class SceneManager {
public:
    using RuntimeSceneLoader = std::function<std::shared_ptr<Scene>(
        const std::string& project_relative_path,
        std::string& error
    )>;

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
    [[nodiscard]] const SceneLoadProgress& play_load_progress() const { return m_play_load_progress; }
    [[nodiscard]] const ScriptRuntime& get_script_runtime() const { return m_script_runtime; }
    void set_renderer(SDL_Renderer* renderer) { m_renderer = renderer; }
    void set_asset_database(const AssetDatabase* asset_database) { m_asset_database = asset_database; }
    void set_script_runtime_services(ScriptRuntimeServices services) { m_script_services = std::move(services); }
    void set_runtime_scene_loader(RuntimeSceneLoader loader) { m_runtime_scene_loader = std::move(loader); }
    void set_on_authoring_state_invalidated(std::function<void()> callback) { m_on_authoring_state_invalidated = std::move(callback); }

    void update(float delta_time);
    void render(Renderer2D& renderer);
    void configure_view(RenderViewKind view, const RenderViewConfiguration& configuration);
    void invalidate_render_resources();
    [[nodiscard]] MaterialAssetRefreshResult refresh_material_assets(const AssetDatabase& database);
    [[nodiscard]] std::size_t preview_material_asset(
        AssetHandle handle,
        std::shared_ptr<const Material> material
    );

    Scene* get_active_scene() const;
    [[nodiscard]] std::shared_ptr<Scene> get_active_scene_shared_ptr() const;
    size_t get_scene_count() const { return m_scenes.size(); }
    int get_active_index() const { return m_active_index; }
    [[nodiscard]] const std::vector<std::shared_ptr<Scene>>& get_scenes() const { return m_scenes; }

private:
    void save_state();
    void apply_active_view();
    void start_scripts(Scene& scene);
    void advance_play_startup();
    [[nodiscard]] bool request_runtime_scene_load(const std::string& path, std::string& error);
    void activate_pending_runtime_scene();

    std::vector<std::shared_ptr<Scene>> m_scenes;
    int m_active_index{-1};
    std::shared_ptr<Scene> m_transient_scene{nullptr};
    std::shared_ptr<Scene> m_runtime_scene{nullptr};
    std::shared_ptr<Scene> m_pending_runtime_scene{nullptr};
    RenderViewConfiguration m_editor_view;
    RenderViewConfiguration m_game_view;

    ScenePlayState m_play_state{ScenePlayState::Edit};
    std::string m_snapshot_json;
    SDL_Renderer* m_renderer{nullptr};
    const AssetDatabase* m_asset_database{nullptr};
    ScriptRuntimeServices m_script_services;
    RuntimeSceneLoader m_runtime_scene_loader;
    std::function<void()> m_on_authoring_state_invalidated;
    ScriptRuntime m_script_runtime;
    enum class PlayStartupPhase { None, Snapshot, PrepareRuntime, StartScripts };
    PlayStartupPhase m_play_startup_phase{PlayStartupPhase::None};
    SceneLoadProgress m_play_load_progress;
};

} // namespace Engine
