#pragma once

#include "../../ecs/EntityManager.hpp"
#include "../../ecs/components/CoreComponents.hpp"
#include "../../renderer/IEntityRenderFilter.hpp"
#include "../state/EditorEntityStateStore.hpp"

namespace Engine {

class EditorEntityVisibilityFilter : public IEntityRenderFilter {
public:
    ~EditorEntityVisibilityFilter() override = default;
    [[nodiscard]] virtual bool should_interact(EntityID id) const = 0;
};

class SceneViewEntityFilter final : public EditorEntityVisibilityFilter {
public:
    explicit SceneViewEntityFilter(const EditorEntityStateStore* store = nullptr, const EntityManager* em = nullptr)
        : m_store(store), m_em(em) {}

    void set_store(const EditorEntityStateStore* store) { m_store = store; }
    [[nodiscard]] const EditorEntityStateStore* get_store() const { return m_store; }

    void set_entity_manager(const EntityManager* em) { m_em = em; }
    [[nodiscard]] const EntityManager* get_entity_manager() const { return m_em; }

    [[nodiscard]] bool is_visible_recursive(EntityID id) const {
        if (!m_store) return true;
        if (!m_store->is_visible(id)) return false;
        if (m_em && m_em->has_component<HierarchyComponent>(id)) {
            EntityID current = m_em->get_component<HierarchyComponent>(id).parent;
            while (current != 0 && m_em->is_alive(current)) {
                if (!m_store->is_visible(current)) return false;
                if (!m_em->has_component<HierarchyComponent>(current)) break;
                current = m_em->get_component<HierarchyComponent>(current).parent;
            }
        }
        return true;
    }

    [[nodiscard]] bool should_render(EntityID id) const override {
        return is_visible_recursive(id);
    }

    [[nodiscard]] bool should_interact(EntityID id) const override {
        if (!m_store) return true;
        return is_visible_recursive(id) && !m_store->is_locked(id);
    }

private:
    const EditorEntityStateStore* m_store{nullptr};
    const EntityManager* m_em{nullptr};
};

} // namespace Engine
