#include "EditorEntityStateStore.hpp"
#include <vector>

namespace Engine {

EditorEntityState EditorEntityStateStore::get(EntityID id) const {
    const auto it = m_states.find(id);
    if (it != m_states.end()) {
        return it->second;
    }
    return EditorEntityState{};
}

bool EditorEntityStateStore::is_locked(EntityID id) const {
    const auto it = m_states.find(id);
    if (it != m_states.end()) {
        return it->second.locked;
    }
    return false;
}

bool EditorEntityStateStore::is_visible(EntityID id) const {
    const auto it = m_states.find(id);
    if (it != m_states.end()) {
        return it->second.visible_in_editor;
    }
    return true;
}

void EditorEntityStateStore::set_locked(EntityID id, bool value) {
    if (id == 0) return;
    auto& state = m_states[id];
    state.locked = value;
    // If state equals defaults, we can clean up the map entry
    if (!state.locked && state.visible_in_editor) {
        m_states.erase(id);
    }
}

void EditorEntityStateStore::set_visible(EntityID id, bool value) {
    if (id == 0) return;
    auto& state = m_states[id];
    state.visible_in_editor = value;
    // If state equals defaults, we can clean up the map entry
    if (!state.locked && state.visible_in_editor) {
        m_states.erase(id);
    }
}

void EditorEntityStateStore::set(EntityID id, EditorEntityState state) {
    if (id == 0) return;
    if (!state.locked && state.visible_in_editor) {
        m_states.erase(id);
    } else {
        m_states[id] = state;
    }
}

void EditorEntityStateStore::remove(EntityID id) {
    m_states.erase(id);
}

void EditorEntityStateStore::prune(const EntityManager& em) {
    std::vector<EntityID> to_remove;
    to_remove.reserve(m_states.size());
    for (const auto& [id, _] : m_states) {
        if (!em.is_alive(id)) {
            to_remove.push_back(id);
        }
    }
    for (EntityID id : to_remove) {
        m_states.erase(id);
    }
}

void EditorEntityStateStore::clear() {
    m_states.clear();
}

} // namespace Engine
