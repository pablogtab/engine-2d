#pragma once

#include "ComponentInspectorTransaction.hpp"

namespace Engine {

class ColliderInspectorDrawer {
public:
    static void draw(EntityManager& em, EntityID entity, const ComponentTransactionContext& context);
};

} // namespace Engine
