#pragma once

#include "EditorPanel.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace Engine {

class EditorCommandHistory;
class EditorSelection;
class Window;
class Scene;
class EditorEntityStateStore;

struct EditorSceneSession {
    std::shared_ptr<EditorCommandHistory> command_history;
    std::shared_ptr<EditorEntityStateStore> entity_state_store;
    std::string file_path;
    std::string initial_snapshot_json;
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
    bool discard_active_scene_changes();
    void load_scene(const std::string& path);
    void create_new_scene(const std::string& name = "Untitled Scene");

private:
    void render_main_menu_bar();
    void setup_default_dockspace_layout(uint32_t dockspace_id);
    void update_panel_command_histories(std::shared_ptr<EditorCommandHistory> ch);
    void update_panel_sessions(std::shared_ptr<EditorCommandHistory> ch, std::shared_ptr<EditorEntityStateStore> store);

    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    Window* m_window_wrapper{nullptr};
    class ViewportPanel* m_viewport_panel{nullptr};
    class SceneManager* m_scene_manager{nullptr};
    std::shared_ptr<EditorCommandHistory> m_command_history{nullptr};
    std::shared_ptr<EditorEntityStateStore> m_entity_state_store{nullptr};
    std::shared_ptr<EditorSelection> m_selection{nullptr};

    std::unordered_map<const Scene*, EditorSceneSession> m_scene_sessions;
    const Scene* m_last_active_scene{nullptr};

    bool m_initialized{false};
    bool m_visible{true}; // Enabled by default in editor builds
    bool m_dockspace_initialized{false};
    bool m_show_save_as_popup{false};
    bool m_show_unsaved_changes_modal{false};
    PendingDestructiveAction m_pending_action;
    char m_save_as_buf[128]{"NewLevel.scene"};

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
