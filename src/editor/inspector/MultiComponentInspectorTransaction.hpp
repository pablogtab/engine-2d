#pragma once

#include "ComponentInspectorTransaction.hpp"
#include "../../ecs/EntityManager.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Engine {

template<typename Component>
using SelectedComponentStates = std::vector<std::pair<EntityID, Component>>;

inline std::vector<EntityID> sorted_selection(std::vector<EntityID> entities) {
    std::sort(entities.begin(), entities.end());
    entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
    return entities;
}

inline std::uint32_t selection_transaction_owner(const std::vector<EntityID>& entities) {
    std::uint32_t hash = 2166136261u;
    for (const EntityID entity : entities) {
        hash ^= entity;
        hash *= 16777619u;
    }
    return hash;
}

template<typename Component>
SelectedComponentStates<Component> capture_selected_components(
    EntityManager& em,
    const std::vector<EntityID>& entities
) {
    SelectedComponentStates<Component> states;
    states.reserve(entities.size());
    for (const EntityID entity : entities) {
        if (em.is_alive(entity) && em.has_component<Component>(entity)) {
            states.emplace_back(entity, em.get_component<Component>(entity));
        }
    }
    return states;
}

template<typename Component, typename Equal>
bool selected_component_states_equal(
    const SelectedComponentStates<Component>& lhs,
    const SelectedComponentStates<Component>& rhs,
    Equal equal
) {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (lhs[index].first != rhs[index].first || !equal(lhs[index].second, rhs[index].second)) {
            return false;
        }
    }
    return true;
}

template<typename Component, typename Value, typename Getter, typename Equal = std::equal_to<Value>>
bool selected_values_mixed(
    const SelectedComponentStates<Component>& states,
    Getter getter,
    Equal equal = Equal{}
) {
    if (states.size() < 2) return false;
    const Value first = getter(states.front().second);
    for (std::size_t index = 1; index < states.size(); ++index) {
        if (!equal(first, getter(states[index].second))) return true;
    }
    return false;
}

template<typename Component, typename Equal>
void track_selected_component_edit(
    EntityManager& em,
    const std::vector<EntityID>& entities,
    const SelectedComponentStates<Component>& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    Equal equal,
    std::function<void(EntityManager&, const std::vector<EntityID>&)> side_effect = {},
    bool force_commit = false
) {
    if (!context.command_history || !context.transactions || entities.empty()) return;

    const auto& interaction = InspectorWidgets::last_property_interaction();
    const std::string transaction_property = std::string("MultiSelection.") + property;
    const auto key = EditorPropertyTransaction::make_key(
        selection_transaction_owner(entities), transaction_property
    );
    if (interaction.activated || (force_commit && !context.transactions->contains(key))) {
        context.transactions->begin<SelectedComponentStates<Component>>(
            key,
            before,
            [&em, entities]() -> std::optional<SelectedComponentStates<Component>> {
                auto current = capture_selected_components<Component>(em, entities);
                if (current.size() != entities.size()) return std::nullopt;
                return current;
            },
            [&em, entities, side_effect](const SelectedComponentStates<Component>& states) {
                for (const auto& [entity, component] : states) {
                    if (em.is_alive(entity) && em.has_component<Component>(entity)) {
                        em.get_component<Component>(entity) = component;
                    }
                }
                if (side_effect) side_effect(em, entities);
            },
            description,
            [equal](const auto& lhs, const auto& rhs) {
                return selected_component_states_equal(lhs, rhs, equal);
            },
            [&em, entities, side_effect]() {
                if (side_effect) side_effect(em, entities);
            }
        );
    }

    if (force_commit) {
        context.transactions->commit(key, *context.command_history);
    } else if (interaction.cancelled) {
        context.transactions->cancel(key);
    } else if (interaction.deactivated_after_edit) {
        context.transactions->commit(key, *context.command_history);
    }
}

} // namespace Engine
