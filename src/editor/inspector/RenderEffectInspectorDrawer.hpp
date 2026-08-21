#pragma once

#include "../../ecs/EcsTypes.hpp"

namespace Engine {

class EditorCommandHistory;
class EntityManager;

class RenderEffectInspectorDrawer {
public:
    static void draw(EntityManager& em, EntityID entity, EditorCommandHistory* command_history);
};

} // namespace Engine
