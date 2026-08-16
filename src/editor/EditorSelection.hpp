#pragma once

#include "../ecs/EntityManager.hpp"
#include <unordered_set>

namespace Engine {

class EditorSelection {
public:
    EditorSelection() = default;

    void select(EntityID id, bool additive = false) {
        if (!additive) {
            m_selected.clear();
            if (id != 0) m_selected.insert(id);
        } else {
            if (m_selected.contains(id)) {
                m_selected.erase(id);
            } else if (id != 0) {
                m_selected.insert(id);
            }
        }
    }

    void set_selection(const std::unordered_set<EntityID>& entities) {
        m_selected = entities;
    }

    void add_selection(const std::unordered_set<EntityID>& entities) {
        for (EntityID id : entities) {
            if (id != 0) m_selected.insert(id);
        }
    }

    void clear() {
        m_selected.clear();
    }

    [[nodiscard]] bool is_selected(EntityID id) const {
        return m_selected.contains(id);
    }

    [[nodiscard]] bool empty() const {
        return m_selected.empty();
    }

    [[nodiscard]] size_t count() const {
        return m_selected.size();
    }

    [[nodiscard]] const std::unordered_set<EntityID>& get_selected() const {
        return m_selected;
    }

    [[nodiscard]] EntityID get_primary() const {
        return m_selected.empty() ? 0 : *m_selected.begin();
    }

private:
    std::unordered_set<EntityID> m_selected;
};

} // namespace Engine
