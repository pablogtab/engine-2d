#pragma once

#include "EditorPanel.hpp"
#include "EditorShell.hpp"
#include "panels/EditorPanelRegistry.hpp"
#include "session/EditorSessionController.hpp"
#include "workspace/WorkspaceLayoutController.hpp"
#include "state/AutosaveRecovery.hpp"
#include "state/RecentSceneList.hpp"
#include "state/EditorPanelStateStore.hpp"
#include "../renderer/LightingBakeService.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <utility>
#include <filesystem>
#include <cstdint>
#include <chrono>
#include <future>

namespace Engine {

class EditorCommandHistory;
class EditorSelection;
class Window;
class Scene;
class EditorEntityStateStore;
class PrefabStageController;
class SpriteAnimationStudioPanel;
class ParticleEffectEditorPanel;
class PostProcessTunerPanel;

enum class PendingActionType {
    None,
    NewScene,
    LoadScene,
    SwitchScene,
    ExitApp
};

struct PendingDestructiveAction {
    PendingActionType type{PendingActionType::None};
    std::string target;
    std::function<void()> action;
};

class EditorLayer {
public:
    EditorLayer() = default;
    ~EditorLayer();

    EditorLayer(const EditorLayer&) = delete;
    EditorLayer& operator=(const EditorLayer&) = delete;

    void init(
        SDL_Window* window,
        SDL_Renderer* renderer,
        const std::filesystem::path& project_root
    );
    void shutdown();

    // Event & Input routing
    void process_event(const SDL_Event& event);
    [[nodiscard]] bool want_capture_mouse() const;
    [[nodiscard]] bool want_capture_keyboard() const;
    [[nodiscard]] bool want_text_input() const;

    // Visibility toggles
    [[nodiscard]] bool is_visible() const { return m_visible; }
    void set_visible(bool visible) { m_visible = visible; }
    void toggle_visibility() { m_visible = !m_visible; }

    // Frame lifecycle
    void begin_frame();
    void render_dockspace();
    void render_panels(float delta_time);
    void end_frame(SDL_Renderer* renderer);
    void invalidate_render_resources();
    void restore_render_resources();
    void trim_render_resources();

    // Modular panel registration
    void register_panel(EditorPanelId id, std::shared_ptr<EditorPanel> panel);
    void unregister_panel(const std::string& name);
    [[nodiscard]] std::shared_ptr<EditorPanel> get_panel(const std::string& name) const;
    [[nodiscard]] std::shared_ptr<EditorPanel> get_panel(EditorPanelId id) const;
    [[nodiscard]] const std::vector<std::shared_ptr<EditorPanel>>& get_panels() const {
        return m_panel_registry.panels();
    }

    [[nodiscard]] class ViewportPanel* get_viewport_panel() const { return m_viewport_panel; }
    void set_viewport_panel(class ViewportPanel* vp) { m_viewport_panel = vp; }
    void set_animation_studio_panel(SpriteAnimationStudioPanel* panel) {
        m_animation_studio_panel = panel;
    }
    void set_particle_editor_panel(ParticleEffectEditorPanel* panel) {
        m_particle_editor_panel = panel;
    }
    void set_post_process_tuner_panel(PostProcessTunerPanel* panel) {
        m_post_process_tuner_panel = panel;
    }
    void set_scene_manager(class SceneManager* sm) { m_scene_manager = sm; }
    void set_window_wrapper(Window* window) { m_window_wrapper = window; }
    void set_command_history(std::shared_ptr<EditorCommandHistory> ch) { m_command_history = ch; }
    [[nodiscard]] std::shared_ptr<EditorCommandHistory> get_command_history() const { return m_command_history; }
    void set_entity_state_store(std::shared_ptr<EditorEntityStateStore> store) { m_entity_state_store = store; }
    [[nodiscard]] std::shared_ptr<EditorEntityStateStore> get_entity_state_store() const { return m_entity_state_store; }
    void set_selection(std::shared_ptr<EditorSelection> sel) { m_selection = sel; }
    [[nodiscard]] std::shared_ptr<EditorSelection> get_selection() const { return m_selection; }

    // Session & Action Pipeline
    EditorSceneSession& get_or_create_session(const Scene* scene, const std::string& default_name = "");
    void bind_scene_path(const Scene* scene, const std::string& path);
    void sync_active_scene_session();
    void cleanup_stale_sessions();
    void update_window_title();
    bool request_destructive_action(PendingDestructiveAction action);
    bool request_exit();
    bool request_project_hub();
    void set_project_hub_request_callback(std::function<void()> callback) {
        m_project_hub_request_callback = std::move(callback);
    }
    bool save_active_scene(const std::string& custom_path = "");
    bool save_all_scenes();
    bool discard_active_scene_changes();
    void load_scene(const std::string& path);
    void activate_scene(std::size_t index);
    void close_scene_tab(std::size_t index);
    [[nodiscard]] bool is_scene_dirty(const Scene* scene) const;
    [[nodiscard]] bool is_scene_tab_open(const Scene* scene) const;
    void create_new_scene(const std::string& name = "Untitled Scene");
    void open_prefab_stage(const std::string& prefab_path);
    [[nodiscard]] PrefabStageController* get_prefab_stage() const;

private:
    void render_main_menu_bar();
    void register_editor_actions();
    void activate_workspace(EditorWorkspaceId id);
    void apply_workspace_panel_visibility(bool restore_saved = true);
    void persist_panel_states();
    [[nodiscard]] bool can_persist_authoring_scenes() const;
    bool save_hot_exit_session();
    void restore_hot_exit_session();
    void update_panel_command_histories(std::shared_ptr<EditorCommandHistory> ch);
    void update_panel_sessions(std::shared_ptr<EditorCommandHistory> ch, std::shared_ptr<EditorEntityStateStore> store, std::shared_ptr<EditorSelection> sel = nullptr);
    void render_prefab_stage_modals();
    bool save_scene(Scene* scene, EditorSceneSession& session, const std::string& custom_path = "");
    void update_autosaves(float delta_time);
    void update_external_scene_changes(float delta_time);
    bool reload_scene_from_disk(Scene* scene, EditorSceneSession& session, const char* reason);
    void refresh_scene_source_stamp(EditorSceneSession& session);
    void render_external_scene_change_modal();
    void schedule_lighting_bake(const std::filesystem::path& scene_path, std::chrono::milliseconds delay);
    void update_lighting_bakes();
    void render_recovery_modal();
    [[nodiscard]] bool recover_autosave(const AutosaveRecord& record);
    void request_save_scene_as(Scene* scene, bool continue_pending_action = false);
    void complete_pending_action();
    void cancel_pending_action();
    void load_autosave_preferences();
    void save_autosave_preferences() const;
    void load_entity_state_preferences(const Scene* scene, EditorSceneSession& session) const;
    void save_entity_state_preferences();
    void record_recent_scene(const std::string& path);
    void open_scene_tab(const Scene* scene);
    [[nodiscard]] bool has_loaded_scene_path(const std::string& path, std::size_t& index);
    [[nodiscard]] std::filesystem::path scenes_directory() const;

    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    Window* m_window_wrapper{nullptr};
    class ViewportPanel* m_viewport_panel{nullptr};
    SpriteAnimationStudioPanel* m_animation_studio_panel{nullptr};
    ParticleEffectEditorPanel* m_particle_editor_panel{nullptr};
    PostProcessTunerPanel* m_post_process_tuner_panel{nullptr};
    class SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};

    EditorSessionController m_sessions;
    RecentSceneList m_recent_scenes;
    AutosaveRecovery m_autosave_recovery;
    std::filesystem::path m_project_root{"."};
    struct LightingBakeJobResult {
        bool success{false};
        LightingBakeStatus status;
        std::string error;
    };
    struct LightingBakeJob {
        std::chrono::steady_clock::time_point deadline{};
        std::chrono::steady_clock::time_point started_at{};
        std::future<LightingBakeJobResult> future;
        bool queued{false};
        bool running{false};
    };
    std::unordered_map<std::string, LightingBakeJob> m_lighting_bake_jobs;
    std::filesystem::path m_recent_scenes_path{"ProjectSettings/EditorRecentScenes.json"};
    std::filesystem::path m_autosave_preferences_path{"ProjectSettings/EditorAutosave.json"};
    std::filesystem::path m_entity_state_preferences_path{"Library/Editor/EntityStates.json"};
    std::filesystem::path m_panel_state_path{"Library/Editor/PanelStates.json"};
    std::filesystem::path m_hot_exit_session_path{"Library/Editor/SessionState.json"};
    EditorPanelStateStore m_panel_states;
    EditorShell m_shell;
    WorkspaceLayoutController m_workspace_layouts;
    std::uint64_t m_saved_panel_state_revision{0};
    std::vector<AutosaveRecord> m_pending_recoveries;
    std::vector<std::string> m_panels_to_reselect;
    const Scene* m_last_active_scene{nullptr};

    bool m_initialized{false};
    bool m_visible{true}; // Enabled by default in editor builds
    bool m_show_save_as_popup{false};
    bool m_show_unsaved_changes_modal{false};
    bool m_show_prefab_stage_exit_modal{false};
    bool m_show_autosave_preferences{false};
    bool m_show_reset_all_layouts_modal{false};
    bool m_recovery_scan_complete{false};
    bool m_hot_exit_restored{false};
    float m_autosave_elapsed{0.0f};
    float m_external_scene_poll_elapsed{0.0f};
    float m_hot_exit_checkpoint_elapsed{0.0f};
    int m_autosave_interval_seconds{60};
    char m_autosave_directory[1024]{"Library/Autosaves"};
    PendingDestructiveAction m_pending_action;
    const Scene* m_external_conflict_scene{nullptr};
    Scene* m_save_as_scene{nullptr};
    std::function<void()> m_project_hub_request_callback;
    char m_save_as_buf[128]{"NewLevel.scene"};
    bool m_continue_pending_action_after_save_as{false};
    std::string m_save_as_error;
    std::string m_unsaved_changes_error;
    std::string m_external_conflict_error;
    std::string m_recovery_error;
    std::string m_prefab_stage_error;

    std::unique_ptr<class PrefabStageController> m_prefab_stage;

    // Keyboard Repeat Acceleration Curve for Undo / Redo
    float m_undo_timer{0.0f};
    float m_undo_interval{0.45f};
    bool m_undo_is_holding{false};

    float m_redo_timer{0.0f};
    float m_redo_interval{0.45f};
    bool m_redo_is_holding{false};

    EditorPanelRegistry m_panel_registry;
};

} // namespace Engine
