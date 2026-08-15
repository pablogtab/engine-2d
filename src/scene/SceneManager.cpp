#include "SceneManager.hpp"
#include "../core/Log.hpp"
#include <fstream>

namespace Engine {

namespace {
    const char* SCENE_STATE_PRIMARY = "build/.scene_state";
    const char* SCENE_STATE_FALLBACK = ".scene_state";
}

void SceneManager::save_state() {
    if (m_active_index < 0 || static_cast<size_t>(m_active_index) >= m_scenes.size()) return;

    std::ofstream out(SCENE_STATE_PRIMARY);
    if (!out.is_open()) {
        out.open(SCENE_STATE_FALLBACK);
    }
    if (out.is_open()) {
        out << m_scenes[m_active_index]->get_name() << "\n";
    }
}

void SceneManager::restore_state() {
    std::ifstream in(SCENE_STATE_PRIMARY);
    if (!in.is_open()) {
        in.open(SCENE_STATE_FALLBACK);
    }
    if (in.is_open()) {
        std::string name;
        if (std::getline(in, name) && !name.empty()) {
            set_active_scene(name);
        }
    }
}

void SceneManager::add_scene(std::shared_ptr<Scene> scene) {
    if (!scene) return;
    
    // Check if scene with same name already exists: replace it instead of duplicating!
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i] && m_scenes[i]->get_name() == scene->get_name()) {
            if (m_active_index == static_cast<int>(i)) {
                m_scenes[i]->on_detach();
            }
            m_scenes[i] = scene;
            if (m_active_index == static_cast<int>(i)) {
                m_scenes[i]->on_attach();
                m_scenes[i]->on_resize(m_last_width, m_last_height, m_last_dpi_scale);
            }
            return;
        }
    }

    m_scenes.push_back(scene);
    
    // Automatically activate first registered scene
    if (m_active_index < 0) {
        set_active_scene(0);
    }
}

void SceneManager::set_active_scene(size_t index) {
    if (index >= m_scenes.size()) return;
    if (m_active_index == static_cast<int>(index)) return; // Prevent redundant double attach!

    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_detach();
    }

    m_active_index = static_cast<int>(index);
    m_scenes[m_active_index]->on_attach();
    m_scenes[m_active_index]->on_resize(m_last_width, m_last_height);

    save_state();

    ENGINE_LOG_INFO("Switched to Scene [{}/{}]: '{}'", m_active_index + 1, m_scenes.size(), m_scenes[m_active_index]->get_name());
}

void SceneManager::set_active_scene(const std::string& name) {
    for (size_t i = 0; i < m_scenes.size(); ++i) {
        if (m_scenes[i]->get_name() == name) {
            set_active_scene(i);
            return;
        }
    }
}

void SceneManager::next_scene() {
    if (m_scenes.empty()) return;
    size_t next_idx = (m_active_index + 1) % m_scenes.size();
    set_active_scene(next_idx);
}

void SceneManager::previous_scene() {
    if (m_scenes.empty()) return;
    size_t prev_idx = (m_active_index - 1 + m_scenes.size()) % m_scenes.size();
    set_active_scene(prev_idx);
}

void SceneManager::update(float delta_time) {
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_update(delta_time);
    }
}

void SceneManager::render(Renderer2D& renderer) {
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_render(renderer);
    }
}

void SceneManager::resize(int width, int height, float dpi_scale) {
    m_last_width = width;
    m_last_height = height;
    m_last_dpi_scale = dpi_scale;
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        m_scenes[m_active_index]->on_resize(width, height, dpi_scale);
    }
}

Scene* SceneManager::get_active_scene() const {
    if (m_active_index >= 0 && static_cast<size_t>(m_active_index) < m_scenes.size()) {
        return m_scenes[m_active_index].get();
    }
    return nullptr;
}

} // namespace Engine
