#include "SpriteAnimationSystem.hpp"
#include "../ecs/components/RenderingComponents.hpp"
#include <algorithm>

namespace Engine {

namespace {

AnimationPlaybackStateComponent& ensure_playback_state(
    EntityManager& em,
    EntityID entity,
    const AnimationComponent& animation
) {
    if (!em.has_component<AnimationPlaybackStateComponent>(entity)) {
        em.add_component<AnimationPlaybackStateComponent>(entity, AnimationPlaybackStateComponent{});
    }
    auto& state = em.get_component<AnimationPlaybackStateComponent>(entity);
    if (!state.initialized) {
        state.active_clip_index = std::clamp(
            animation.default_clip_index,
            0,
            std::max(0, static_cast<int>(animation.clips.size()) - 1)
        );
        if (const auto* clip = animation.get_clip(state.active_clip_index)) {
            state.current_frame = clip->custom_frames.empty() ? clip->start_frame : 0;
        }
        state.is_playing = animation.play_on_start;
        state.is_finished = false;
        state.timer = 0.0f;
        state.initialized = true;
    }
    return state;
}

} // namespace

void SpriteAnimationSystem::update(EntityManager& em, float delta_time) {
    em.for_each<AnimationComponent, SpriteComponent>([&em, delta_time](EntityID entity, AnimationComponent& anim, SpriteComponent& sprite) {
        auto& state = ensure_playback_state(em, entity, anim);
        const AnimationClip* clip = anim.get_clip(state.active_clip_index);
        if (!clip) return;
        int total_frames = clip->get_total_frames();
        if (total_frames <= 0) return;

        if (state.is_playing) {
            state.timer += delta_time;
            float frame_duration = 1.0f / (clip->fps > 0.1f ? clip->fps : 10.0f);

            while (state.timer >= frame_duration && state.is_playing) {
                state.timer -= frame_duration;
                int rel_frame = clip->custom_frames.empty() ? (state.current_frame - clip->start_frame) : state.current_frame;
                rel_frame++;

                if (rel_frame >= total_frames) {
                    if (clip->loop) {
                        rel_frame = 0;
                    } else {
                        rel_frame = total_frames - 1;
                        state.is_playing = false;
                        state.is_finished = true;
                    }
                }
                state.current_frame = clip->custom_frames.empty() ? (clip->start_frame + rel_frame) : rel_frame;
            }
        }

        // Apply UV frame crop to SpriteComponent
        sprite.src_rect = anim.get_frame_rect(state.current_frame, clip);
        sprite.use_src_rect = true;
    });
}

bool SpriteAnimationSystem::play_clip(
    EntityManager& em,
    EntityID entity,
    std::string_view clip_name,
    bool restart
) {
    if (!em.is_alive(entity) || !em.has_component<AnimationComponent>(entity)) return false;
    const auto& animation = em.get_component<AnimationComponent>(entity);
    for (std::size_t index = 0; index < animation.clips.size(); ++index) {
        if (animation.clips[index].name != clip_name) continue;
        auto& state = ensure_playback_state(em, entity, animation);
        if (restart || state.active_clip_index != static_cast<int>(index)) {
            state.active_clip_index = static_cast<int>(index);
            state.current_frame = animation.clips[index].custom_frames.empty()
                ? animation.clips[index].start_frame
                : 0;
            state.timer = 0.0f;
            state.is_finished = false;
        }
        state.is_playing = true;
        return true;
    }
    return false;
}

void SpriteAnimationSystem::reset_playback(EntityManager& em, EntityID entity) {
    if (!em.is_alive(entity)) return;
    if (em.has_component<AnimationPlaybackStateComponent>(entity)) {
        em.remove_component<AnimationPlaybackStateComponent>(entity);
    }
}

AnimationPlaybackStateComponent* SpriteAnimationSystem::get_playback_state(EntityManager& em, EntityID entity) {
    if (!em.is_alive(entity) || !em.has_component<AnimationPlaybackStateComponent>(entity)) return nullptr;
    return &em.get_component<AnimationPlaybackStateComponent>(entity);
}

} // namespace Engine
