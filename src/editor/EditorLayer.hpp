#pragma once

#include "EditorPanel.hpp"
#include "state/AutosaveRecovery.hpp"
#include "state/RecentSceneList.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <utility>

namespace Engine {

class EditorCommandHistory;
class EditorSelection;
class Window;
class Scene;
class EditorEntityStateStore;
class PrefabStageController;

struct EditorSceneSession {
    std::shared_ptr<EditorCommandHistory> command_history;
    std::shared_ptr<EditorEntityStateStore> entity_state_store;
    std::shared_ptr<EditorSelection> selection;
    std::string file_path;
    std::string initial_snapshot_json;
    std::string autosave_identity;
    bool recovery_source_conflicted{false};
};

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

    void init(SDL_Window* window, SDL_Renderer* renderer);
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

    // Modular panel registration
    void register_panel(std::shared_ptr<EditorPanel> panel);
    void unregister_panel(const std::string& name);
    [[nodiscard]] std::shared_ptr<EditorPanel> get_panel(const std::string& name) const;
    [[nodiscard]] const std::vector<std::shared_ptr<EditorPanel>>& get_panels() const { return m_panels; }

    [[nodiscard]] class ViewportPanel* get_viewport_panel() const { return m_viewport_panel; }
    void set_viewport_panel(class ViewportPanel* vp) { m_viewport_panel = vp; }
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
    void sync_active_scene_session();
    void cleanup_stale_sessions();
    void update_window_title();
    bool request_destructive_action(PendingDestructiveAction action);
    bool request_exit();
    bool save_active_scene(const std::string& custom_path = "");
    bool save_all_scenes();
    bool discard_active_scene_changes();
    void load_scene(const std::string& path);
    void activate_scene(std::size_t index);
    [[nodiscard]] bool is_scene_dirty(const Scene* scene) const;
    [[nodiscard]] bool is_scene_tab_open(const Scene* scene) const;
    void create_new_scene(const std::string& name = "Untitled Scene");
    void open_prefab_stage(const std::string& prefab_path);
    [[nodiscard]] PrefabStageController* get_prefab_stage() const;

private:
    void render_main_menu_bar();
    void setup_default_dockspace_layout(uint32_t dockspace_id);
    void update_panel_command_histories(std::shared_ptr<EditorCommandHistory> ch);
    void update_panel_sessions(std::shared_ptr<EditorCommandHistory> ch, std::shared_ptr<EditorEntityStateStore> store, std::shared_ptr<EditorSelection> sel = nullptr);
    void render_prefab_stage_modals();
    bool save_scene(Scene* scene, EditorSceneSession& session, const std::string& custom_path = "");
    void update_autosaves(float delta_time);
    void render_recovery_modal();
    void recover_autosave(const AutosaveRecord& record);
    void load_autosave_preferences();
    void save_autosave_preferences() const;
    void record_recent_scene(const std::string& path);
    void open_scene_tab(const Scene* scene);
    [[nodiscard]] bool has_loaded_scene_path(const std::string& path, std::size_t& index);

    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    Window* m_window_wrapper{nullptr};
    class ViewportPanel* m_viewport_panel{nullptr};
    class SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};

    std::unordered_map<const Scene*, EditorSceneSession> m_scene_sessions;
    std::vector<const Scene*> m_open_scene_tabs;
    RecentSceneList m_recent_scenes;
    AutosaveRecovery m_autosave_recovery;
    std::vector<AutosaveRecord> m_pending_recoveries;
    const Scene* m_last_active_scene{nullptr};

    bool m_initialized{false};
    bool m_visible{true}; // Enabled by default in editor builds
    bool m_dockspace_initialized{false};
    bool m_show_save_as_popup{false};
    bool m_show_unsaved_changes_modal{false};
    bool m_show_prefab_stage_exit_modal{false};
    bool m_show_autosave_preferences{false};
    bool m_recovery_scan_complete{false};
    float m_autosave_elapsed{0.0f};
    int m_autosave_interval_seconds{60};
    char m_autosave_directory[256]{"Library/Autosaves"};
    PendingDestructiveAction m_pending_action;
    char m_save_as_buf[128]{"NewLevel.scene"};

    std::unique_ptr<class PrefabStageController> m_prefab_stage;

    // Keyboard Repeat Acceleration Curve for Undo / Redo
    float m_undo_timer{0.0f};
    float m_undo_interval{0.45f};
    bool m_undo_is_holding{false};

    float m_redo_timer{0.0f};
    float m_redo_interval{0.45f};
    bool m_redo_is_holding{false};

    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};

} // namespace Engine
