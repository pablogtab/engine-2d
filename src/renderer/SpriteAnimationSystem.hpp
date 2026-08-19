#pragma once

#include "../ecs/EntityManager.hpp"
#include "../ecs/components/AnimationComponents.hpp"
#include <string_view>

namespace Engine {

class SpriteAnimationSystem {
public:
    static void update(EntityManager& em, float delta_time);
    static bool play_clip(EntityManager& em, EntityID entity, std::string_view clip_name, bool restart = false);
    static void reset_playback(EntityManager& em, EntityID entity);
    static AnimationPlaybackStateComponent* get_playback_state(EntityManager& em, EntityID entity);
};

} // namespace Engine
