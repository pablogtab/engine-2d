#pragma once

#include "ComponentInspectorTransaction.hpp"

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
};

} // namespace Engine
