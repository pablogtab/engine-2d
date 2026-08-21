#pragma once

#include "ComponentInspectorTransaction.hpp"
#include "MaterialInspectorWidget.hpp"

struct SDL_Renderer;

namespace Engine {

class UIInspectorDrawer {
public:
    static void draw_canvas(EntityManager& em, EntityID entity, const ComponentTransactionContext& context);
    static void draw_layout(EntityManager& em, EntityID entity, const ComponentTransactionContext& context);
    static void draw_text(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context,
        SDL_Renderer* renderer = nullptr,
        const MaterialInspectorActions& material_actions = {}
    );
    static void draw_image(
        EntityManager& em,
        EntityID entity,
        const ComponentTransactionContext& context,
        SDL_Renderer* renderer = nullptr,
        const MaterialInspectorActions& material_actions = {}
    );
    static void draw_button(EntityManager& em, EntityID entity, const ComponentTransactionContext& context);
};

} // namespace Engine
