#pragma once

#include "ComponentInspectorTransaction.hpp"
#include <SDL3/SDL.h>

namespace Engine {

class AssetManager;

class LightingInspectorDrawer {
public:
    struct SpriteLightingContext {
        ComponentTransactionContext transaction;
        SDL_Renderer* renderer{nullptr};
        AssetManager* asset_manager{nullptr};
    };
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
        const SpriteLightingContext& context
    );
    static void draw_sprite_shadow(
        EntityManager& em,
        EntityID entity,
        const SpriteLightingContext& context
    );
    static void draw_bake_volume(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context
    );
};

} // namespace Engine
