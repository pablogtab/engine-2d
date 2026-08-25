#pragma once

#include "EditorComponentRegistry.hpp"

namespace Engine {

class GameplayTagsInspectorDrawer {
public:
    static void draw(
        EntityManager& entity_manager,
        EntityID entity,
        const EditorInspectorContext& context
    );
};

} // namespace Engine
