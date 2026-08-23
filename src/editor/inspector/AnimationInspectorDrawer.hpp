#pragma once

#include "../../ecs/EcsTypes.hpp"
#include "../../ecs/components/AnimationComponents.hpp"

namespace Engine {

class EditorCommandHistory;
class EntityManager;
class SpriteAnimationStudioPanel;

class AnimationInspectorDrawer {
public:
    struct Context {
        EditorCommandHistory* command_history{nullptr};
        SpriteAnimationStudioPanel* studio{nullptr};
    };

    static void draw(EntityManager& em, EntityID entity, const Context& context);
};

} // namespace Engine
