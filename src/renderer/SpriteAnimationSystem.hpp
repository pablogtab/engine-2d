#pragma once

#include "../ecs/EntityManager.hpp"
#include "../ecs/Components.hpp"

namespace Engine {

class SpriteAnimationSystem {
public:
    static void update(EntityManager& em, float delta_time);
};

} // namespace Engine
