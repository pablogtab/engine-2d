#include "EditorEntityStateStore.hpp"
#include <algorithm>
#include <unordered_set>
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

std::vector<std::string> EditorEntityStateStore::get_component_order(EntityID id) const {
    const auto it = m_states.find(id);
    return it != m_states.end() ? it->second.component_order : std::vector<std::string>{};
}

std::vector<std::string> EditorEntityStateStore::resolve_component_order(
    EntityID id,
    const std::vector<std::string>& present_components
) const {
    std::vector<std::string> resolved;
    resolved.reserve(present_components.size());
    std::unordered_set<std::string> present(present_components.begin(), present_components.end());
    std::unordered_set<std::string> used;

    for (const std::string& key : get_component_order(id)) {
        if (present.contains(key) && used.insert(key).second) resolved.push_back(key);
    }
    for (const std::string& key : present_components) {
        if (used.insert(key).second) resolved.push_back(key);
    }
    return resolved;
}

std::vector<std::pair<EntityID, std::vector<std::string>>> EditorEntityStateStore::component_orders() const {
    std::vector<std::pair<EntityID, std::vector<std::string>>> result;
    for (const auto& [id, state] : m_states) {
        if (!state.component_order.empty()) result.emplace_back(id, state.component_order);
    }
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    return result;
}

std::vector<std::pair<EntityID, EditorEntityState>> EditorEntityStateStore::states() const {
    std::vector<std::pair<EntityID, EditorEntityState>> result(m_states.begin(), m_states.end());
    std::sort(result.begin(), result.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });
    return result;
}

void EditorEntityStateStore::erase_if_default(EntityID id) {
    const auto it = m_states.find(id);
    if (it != m_states.end() && !it->second.locked && it->second.visible_in_editor &&
        it->second.component_order.empty()) {
        m_states.erase(it);
    }
}

void EditorEntityStateStore::set_locked(EntityID id, bool value) {
    if (id == 0) return;
    if (is_locked(id) == value) return;
    auto& state = m_states[id];
    state.locked = value;
    erase_if_default(id);
    ++m_persistence_revision;
}

void EditorEntityStateStore::set_visible(EntityID id, bool value) {
    if (id == 0) return;
    if (is_visible(id) == value) return;
    auto& state = m_states[id];
    state.visible_in_editor = value;
    erase_if_default(id);
    ++m_persistence_revision;
}

void EditorEntityStateStore::set_component_order(EntityID id, std::vector<std::string> order) {
    if (id == 0) return;
    auto& state = m_states[id];
    state.component_order = std::move(order);
    erase_if_default(id);
}

void EditorEntityStateStore::clear_component_orders() {
    std::vector<EntityID> defaults;
    for (auto& [id, state] : m_states) {
        state.component_order.clear();
        if (!state.locked && state.visible_in_editor) defaults.push_back(id);
    }
    for (const EntityID id : defaults) m_states.erase(id);
}

void EditorEntityStateStore::set(EntityID id, EditorEntityState state) {
    if (id == 0) return;
    const EditorEntityState previous = get(id);
    const bool persistence_changed = previous.locked != state.locked ||
        previous.visible_in_editor != state.visible_in_editor;
    if (!state.locked && state.visible_in_editor && state.component_order.empty()) {
        m_states.erase(id);
    } else {
        m_states[id] = state;
    }
    if (persistence_changed) ++m_persistence_revision;
}

void EditorEntityStateStore::remove(EntityID id) {
    const auto it = m_states.find(id);
    if (it != m_states.end() && (it->second.locked || !it->second.visible_in_editor)) {
        ++m_persistence_revision;
    }
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
        remove(id);
    }
}

void EditorEntityStateStore::clear() {
    if (std::any_of(m_states.begin(), m_states.end(), [](const auto& entry) {
            return entry.second.locked || !entry.second.visible_in_editor;
        })) {
        ++m_persistence_revision;
    }
    m_states.clear();
}

} // namespace Engine
