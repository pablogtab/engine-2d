#pragma once

#include "ComponentInspectorTransaction.hpp"

namespace Engine {

class LightingInspectorDrawer {
public:
    static void draw_light(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
    static void draw_shadow_caster(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
    static void draw_sprite_lighting(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
    static void draw_sprite_shadow(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
    static void draw_bake_volume(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
};

} // namespace Engine
