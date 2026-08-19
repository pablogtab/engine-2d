#pragma once

#include "ComponentInspectorTransaction.hpp"
#include <vector>

namespace Engine {

class TransformInspectorDrawer {
public:
    static void draw_entity_active(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
    static void draw(
        EntityManager& em,
        EntityID entity,
        bool locked,
        const ComponentTransactionContext& context
    );

    static void draw_entities_active(
        EntityManager& em,
        const std::vector<EntityID>& entities,
        const ComponentTransactionContext& context
    );

    static void draw_multiple(
        EntityManager& em,
        const std::vector<EntityID>& entities,
        bool locked,
        const ComponentTransactionContext& context
    );
};

} // namespace Engine
