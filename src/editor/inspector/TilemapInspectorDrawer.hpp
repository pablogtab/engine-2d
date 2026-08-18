#pragma once

#include "ComponentInspectorTransaction.hpp"
#include "../../tilemap/Tilemap.hpp"

struct SDL_Renderer;

namespace Engine {

class AssetManager;

class TilemapInspectorDrawer {
public:
    struct Context {
        SDL_Renderer* renderer{nullptr};
        AssetManager* asset_manager{nullptr};
        EditorCommandHistory* command_history{nullptr};
        EditorPropertyTransaction* transactions{nullptr};
    };

    static void draw(EntityManager& em, EntityID entity, const Context& context);
};

} // namespace Engine
