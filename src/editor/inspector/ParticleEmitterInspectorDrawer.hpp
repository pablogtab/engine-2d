#pragma once

#include "EditorComponentRegistry.hpp"
#include "../../ecs/EntityManager.hpp"

namespace Engine {

class ParticleEmitterInspectorDrawer {
public:
    static void draw(
        EntityManager& em,
        EntityID entity,
        const EditorInspectorContext& context
    );
};

} // namespace Engine
