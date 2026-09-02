#include "EditorSessionController.hpp"

#include <algorithm>
#include <stdexcept>

namespace Engine {

EditorSceneSession& EditorSessionController::ensure(
    const Scene* scene,
    SessionFactory factory
) {
    if (!scene) throw std::invalid_argument("Editor session requires a scene");
    const auto existing = m_sessions.find(scene);
    if (existing != m_sessions.end()) return existing->second;
    return m_sessions.emplace(scene, factory()).first->second;
}

EditorSceneSession* EditorSessionController::find(const Scene* scene) {
    const auto it = m_sessions.find(scene);
    return it == m_sessions.end() ? nullptr : &it->second;
}

const EditorSceneSession* EditorSessionController::find(const Scene* scene) const {
    const auto it = m_sessions.find(scene);
    return it == m_sessions.end() ? nullptr : &it->second;
}

void EditorSessionController::erase(const Scene* scene) {
    m_sessions.erase(scene);
    close_tab(scene);
}

void EditorSessionController::prune(const std::function<bool(const Scene*)>& keep) {
    std::erase_if(m_sessions, [&keep](const auto& entry) { return !keep(entry.first); });
    std::erase_if(m_open_tabs, [&keep](const Scene* scene) { return !keep(scene); });
}

bool EditorSessionController::is_tab_open(const Scene* scene) const {
    return std::find(m_open_tabs.begin(), m_open_tabs.end(), scene) != m_open_tabs.end();
}

void EditorSessionController::open_tab(const Scene* scene) {
    if (scene && !is_tab_open(scene)) m_open_tabs.push_back(scene);
}

void EditorSessionController::close_tab(const Scene* scene) {
    std::erase(m_open_tabs, scene);
}

} // namespace Engine
