#include "EditorPanelRegistry.hpp"

#include <algorithm>

namespace Engine {

bool EditorPanelRegistry::add(
    EditorPanelId id,
    std::shared_ptr<EditorPanel> panel,
    const EditorPanelContext& context
) {
    if (!panel || id == EditorPanelId::Unknown || find(id)) return false;
    panel->set_id(id);
    panel->on_context_changed(context);
    m_panels.push_back(std::move(panel));
    return true;
}

bool EditorPanelRegistry::remove(const std::string& name) {
    const auto it = std::find_if(m_panels.begin(), m_panels.end(), [&name](const auto& panel) {
        return panel && panel->get_name() == name;
    });
    if (it == m_panels.end()) return false;
    m_panels.erase(it);
    return true;
}

std::shared_ptr<EditorPanel> EditorPanelRegistry::find(const std::string& name) const {
    const auto it = std::find_if(m_panels.begin(), m_panels.end(), [&name](const auto& panel) {
        return panel && panel->get_name() == name;
    });
    return it == m_panels.end() ? nullptr : *it;
}

std::shared_ptr<EditorPanel> EditorPanelRegistry::find(EditorPanelId id) const {
    const auto it = std::find_if(m_panels.begin(), m_panels.end(), [id](const auto& panel) {
        return panel && panel->get_id() == id;
    });
    return it == m_panels.end() ? nullptr : *it;
}

void EditorPanelRegistry::broadcast_context(const EditorPanelContext& context) {
    for (const auto& panel : m_panels) {
        if (panel) panel->on_context_changed(context);
    }
}

void EditorPanelRegistry::invalidate_render_resources() {
    for (const auto& panel : m_panels) {
        if (panel) panel->on_render_resources_invalidated();
    }
}

} // namespace Engine
