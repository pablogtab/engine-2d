#pragma once

#include "../commands/EditorCommandHistory.hpp"
#include "../commands/EditorPropertyTransaction.hpp"
#include "../../ecs/Components.hpp"

struct SDL_Renderer;

namespace Engine {

class AssetManager;
class EntityManager;

class SpriteInspectorDrawer {
public:
    struct Context {
        SDL_Renderer* renderer{nullptr};
        AssetManager* asset_manager{nullptr};
        EditorCommandHistory* command_history{nullptr};
        EditorPropertyTransaction* transactions{nullptr};
    };

    static void draw(EntityManager& em, EntityID entity, const Context& context);

private:
    static void track_edit(
        EntityManager& em,
        EntityID entity,
        const SpriteComponent& before,
        const char* property,
        const char* description,
        const Context& context,
        bool force_commit = false
    );
};

} // namespace Engine
