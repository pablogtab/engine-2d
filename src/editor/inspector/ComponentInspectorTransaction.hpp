#pragma once

#include "InspectorWidgets.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include "../commands/EditorCommandHistory.hpp"
#include "../../ecs/EntityManager.hpp"
#include <functional>
#include <optional>

namespace Engine {

struct ComponentTransactionContext {
    EditorCommandHistory* command_history{nullptr};
    EditorPropertyTransaction* transactions{nullptr};
};

template<typename Component, typename Equal>
void track_component_edit(
    EntityManager& em,
    EntityID entity,
    const Component& before,
    const char* property,
    const char* description,
    const ComponentTransactionContext& context,
    Equal equal,
    std::function<void(EntityManager&, EntityID)> side_effect = {},
    bool force_commit = false
) {
    if (!context.command_history || !context.transactions) return;

    const auto& interaction = InspectorWidgets::last_property_interaction();
    const auto key = EditorPropertyTransaction::make_key(entity, property);
    if (interaction.activated || (force_commit && !context.transactions->contains(key))) {
        context.transactions->begin<Component>(
            key,
            before,
            [&em, entity]() -> std::optional<Component> {
                if (!em.is_alive(entity) || !em.has_component<Component>(entity)) return std::nullopt;
                return em.get_component<Component>(entity);
            },
            [&em, entity, side_effect](const Component& value) {
                if (!em.is_alive(entity) || !em.has_component<Component>(entity)) return;
                em.get_component<Component>(entity) = value;
                if (side_effect) side_effect(em, entity);
            },
            description,
            std::move(equal),
            [&em, entity, side_effect]() {
                if (side_effect && em.is_alive(entity)) side_effect(em, entity);
            }
        );
    }

    if (interaction.cancelled) {
        context.transactions->cancel(key);
    } else if (force_commit || interaction.deactivated_after_edit) {
        context.transactions->commit(key, *context.command_history);
    }
}

} // namespace Engine
