#pragma once

#include "ComponentInspectorTransaction.hpp"

namespace Engine {

class RigidBodyInspectorDrawer {
public:
    static void draw(EntityManager& em, EntityID entity, const ComponentTransactionContext& context);
    static void draw_multiple(EntityManager& em, const std::vector<EntityID>& entities, const ComponentTransactionContext& context);
};

} // namespace Engine
