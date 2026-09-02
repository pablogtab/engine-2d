#pragma once

#include "ComponentInspectorTransaction.hpp"

namespace Engine {

class AssetManager;
struct EditorInspectorContext;

class TrailRendererInspectorDrawer {
public:
    static void draw(
        EntityManager& entities,
        EntityID entity,
        const EditorInspectorContext& context
    );
};

} // namespace Engine
