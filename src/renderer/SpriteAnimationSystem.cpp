#include "SpriteAnimationSystem.hpp"
#include <algorithm>

namespace Engine {

void SpriteAnimationSystem::update(EntityManager& em, float delta_time) {
    em.for_each<AnimationComponent, SpriteComponent>([delta_time](EntityID, AnimationComponent& anim, SpriteComponent& sprite) {
        const AnimationClip* clip = anim.get_active_clip();
        if (!clip) return;
        int total_frames = clip->get_total_frames();
        if (total_frames <= 0) return;

        if (anim.is_playing) {
            anim.timer += delta_time;
            float frame_duration = 1.0f / (clip->fps > 0.1f ? clip->fps : 10.0f);

            while (anim.timer >= frame_duration && anim.is_playing) {
                anim.timer -= frame_duration;
                int rel_frame = clip->custom_frames.empty() ? (anim.current_frame - clip->start_frame) : anim.current_frame;
                rel_frame++;

                if (rel_frame >= total_frames) {
                    if (clip->loop) {
                        rel_frame = 0;
                    } else {
                        rel_frame = total_frames - 1;
                        anim.is_playing = false;
                        anim.is_finished = true;
                    }
                }
                anim.current_frame = clip->custom_frames.empty() ? (clip->start_frame + rel_frame) : rel_frame;
            }
        }

        // Apply UV frame crop to SpriteComponent
        sprite.src_rect = anim.get_frame_rect(anim.current_frame, clip);
        sprite.use_src_rect = true;
    });
}

} // namespace Engine
