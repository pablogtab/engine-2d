#pragma once

#include "../../ecs/EntityManager.hpp"
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Engine {

struct EditorEntityState {
    bool locked{false};
    bool visible_in_editor{true};
    std::vector<std::string> component_order;
};

class EditorEntityStateStore {
public:
    EditorEntityStateStore() = default;

    [[nodiscard]] EditorEntityState get(EntityID id) const;
    [[nodiscard]] bool is_locked(EntityID id) const;
    [[nodiscard]] bool is_visible(EntityID id) const;
    [[nodiscard]] std::vector<std::string> get_component_order(EntityID id) const;
    [[nodiscard]] std::vector<std::string> resolve_component_order(
        EntityID id,
        const std::vector<std::string>& present_components
    ) const;
    [[nodiscard]] std::vector<std::pair<EntityID, std::vector<std::string>>> component_orders() const;

    void set_locked(EntityID id, bool value);
    void set_visible(EntityID id, bool value);
    void set_component_order(EntityID id, std::vector<std::string> order);
    void clear_component_orders();
    void set(EntityID id, EditorEntityState state);
    void remove(EntityID id);
    void prune(const EntityManager& em);
    void clear();

private:
    void erase_if_default(EntityID id);

    std::unordered_map<EntityID, EditorEntityState> m_states;
};

} // namespace Engine
