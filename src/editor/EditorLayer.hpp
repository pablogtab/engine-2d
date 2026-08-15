#pragma once

#include "EditorPanel.hpp"
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <string>

namespace Engine {

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

private:
    void render_main_menu_bar();
    void setup_default_dockspace_layout(uint32_t dockspace_id);
    void save_active_scene(const std::string& custom_path = "");
    void load_scene(const std::string& path);
    void create_new_scene(const std::string& name = "Untitled Scene");

    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    class ViewportPanel* m_viewport_panel{nullptr};
    class SceneManager* m_scene_manager{nullptr};
    bool m_initialized{false};
    bool m_visible{true}; // Enabled by default in editor builds
    bool m_dockspace_initialized{false};
    bool m_show_save_as_popup{false};
    char m_save_as_buf[128]{"NewLevel.scene"};

    std::vector<std::shared_ptr<EditorPanel>> m_panels;
};

} // namespace Engine
