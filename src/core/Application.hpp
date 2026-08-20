#pragma once

#include "Window.hpp"
#include "AssetDatabase.hpp"
#include "AssetManager.hpp"
#include "ThreadPool.hpp"
#include "ProjectSettings.hpp"
#include "DisplayManager.hpp"
#include "renderer/Renderer2D.hpp"
#include "scene/SceneManager.hpp"
#include <memory>

namespace Engine {

class Application {
public:
    Application(
        const std::string& name = "2D Engine",
        int width = 1280,
        int height = 720,
        bool start_with_editor = true
    );
    virtual ~Application();

    void run();

    SceneManager& get_scene_manager() { return m_scene_manager; }
    AssetDatabase& get_asset_database() { return AssetDatabase::get(); }
    AssetManager& get_asset_manager() { return AssetManager::get(); }
    ThreadPool& get_thread_pool() { return m_thread_pool; }
    Window& get_window() { return *m_window; }
    DisplayManager& get_display() { return *m_display_manager; }
    const DisplayManager& get_display() const { return *m_display_manager; }
    const ProjectSettings& get_project_settings() const { return m_project_settings; }
    Renderer2D* get_renderer() { return m_renderer.get(); }
    class EditorLayer* get_editor_layer() { return m_editor_layer.get(); }

protected:
    virtual void on_init() {}
    virtual void on_update(float delta_time) {}
    virtual void on_render() {}
    virtual void on_resize(int width, int height) {}
    virtual void on_display_changed(const DisplayState& state) {}
    virtual void on_shutdown() {}

private:
    void apply_display_mode(bool editor_visible);

    std::unique_ptr<Window> m_window;
    std::unique_ptr<DisplayManager> m_display_manager;
    std::unique_ptr<Renderer2D> m_renderer;
    std::unique_ptr<class EditorLayer> m_editor_layer;
    SceneManager m_scene_manager;
    ThreadPool m_thread_pool;
    ProjectSettings m_project_settings;
    bool m_running{true};
};

} // namespace Engine
