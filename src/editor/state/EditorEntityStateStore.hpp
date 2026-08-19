#pragma once

#include "../../ecs/EntityManager.hpp"
#include <unordered_map>

namespace Engine {

struct EditorEntityState {
    bool locked{false};
    bool visible_in_editor{true};
};

class EditorEntityStateStore {
public:
    EditorEntityStateStore() = default;

    [[nodiscard]] EditorEntityState get(EntityID id) const;
    [[nodiscard]] bool is_locked(EntityID id) const;
    [[nodiscard]] bool is_visible(EntityID id) const;

    void set_locked(EntityID id, bool value);
    void set_visible(EntityID id, bool value);
    void set(EntityID id, EditorEntityState state);
    void remove(EntityID id);
    void prune(const EntityManager& em);
    void clear();

private:
    std::unordered_map<EntityID, EditorEntityState> m_states;
};

} // namespace Engine
