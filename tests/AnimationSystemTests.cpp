#include "ecs/components/AnimationComponents.hpp"
#include "ecs/components/RenderingComponents.hpp"
#include "ecs/EntityManager.hpp"
#include "renderer/SpriteAnimationSystem.hpp"
#include <cassert>
#include <iostream>

namespace {

using namespace Engine;

EntityID create_animated_entity(EntityManager& em, AnimationComponent animation) {
    const EntityID entity = em.create_entity();
    em.add_component<AnimationComponent>(entity, std::move(animation));
    em.add_component<SpriteComponent>(entity, SpriteComponent{});
    return entity;
}

void test_runtime_state_is_separate_from_authored_animation() {
    EntityManager em;
    AnimationComponent animation;
    animation.columns = 4;
    animation.rows = 2;
    animation.frame_width = 16.0f;
    animation.frame_height = 24.0f;
    animation.default_clip_index = 1;
    animation.play_on_start = true;
    animation.add_clip("Idle", 0, 2, 10.0f, true);
    animation.add_clip("Run", 4, 3, 10.0f, true);
    const AnimationComponent authored = animation;
    const EntityID entity = create_animated_entity(em, animation);

    SpriteAnimationSystem::update(em, 0.11f);
    assert(em.get_component<AnimationComponent>(entity) == authored);
    const auto* state = SpriteAnimationSystem::get_playback_state(em, entity);
    assert(state);
    assert(state->active_clip_index == 1);
    assert(state->current_frame == 5);
    assert(state->is_playing);
    assert(em.get_component<SpriteComponent>(entity).src_rect.x == 16.0f);
    assert(em.get_component<SpriteComponent>(entity).src_rect.y == 24.0f);
}

void test_play_clip_and_non_loop_completion() {
    EntityManager em;
    AnimationComponent animation;
    animation.columns = 8;
    animation.play_on_start = false;
    animation.add_clip("Idle", 0, 2, 10.0f, true);
    animation.add_clip("Jump", 4, 2, 10.0f, false);
    const EntityID entity = create_animated_entity(em, animation);

    SpriteAnimationSystem::update(em, 0.0f);
    auto* state = SpriteAnimationSystem::get_playback_state(em, entity);
    assert(state && !state->is_playing);
    assert(SpriteAnimationSystem::play_clip(em, entity, "Jump"));
    assert(!SpriteAnimationSystem::play_clip(em, entity, "Missing"));

    SpriteAnimationSystem::update(em, 0.25f);
    state = SpriteAnimationSystem::get_playback_state(em, entity);
    assert(state->active_clip_index == 1);
    assert(state->current_frame == 5);
    assert(!state->is_playing);
    assert(state->is_finished);
}

void test_custom_frames_and_reset() {
    EntityManager em;
    AnimationComponent animation;
    animation.play_on_start = true;
    AnimationClip clip("Custom", 0, 1, 5.0f, true);
    clip.custom_frames = {
        SDL_FRect{2.0f, 3.0f, 10.0f, 11.0f},
        SDL_FRect{20.0f, 30.0f, 12.0f, 13.0f}
    };
    animation.clips.push_back(clip);
    const EntityID entity = create_animated_entity(em, animation);

    SpriteAnimationSystem::update(em, 0.21f);
    const auto& sprite = em.get_component<SpriteComponent>(entity);
    assert(sprite.src_rect.x == 20.0f);
    assert(sprite.src_rect.y == 30.0f);

    SpriteAnimationSystem::reset_playback(em, entity);
    assert(!em.has_component<AnimationPlaybackStateComponent>(entity));
    SpriteAnimationSystem::update(em, 0.0f);
    const auto* state = SpriteAnimationSystem::get_playback_state(em, entity);
    assert(state && state->current_frame == 0);
}

void test_grid_spacing_margins_and_looping() {
    EntityManager em;
    AnimationComponent animation;
    animation.columns = 3;
    animation.rows = 2;
    animation.frame_width = 10.0f;
    animation.frame_height = 20.0f;
    animation.spacing_x = 2.0f;
    animation.spacing_y = 4.0f;
    animation.margin_x = 5.0f;
    animation.margin_y = 7.0f;
    animation.add_clip("Loop", 2, 2, 10.0f, true);
    const EntityID entity = create_animated_entity(em, animation);

    SpriteAnimationSystem::update(em, 0.21f);
    const auto* state = SpriteAnimationSystem::get_playback_state(em, entity);
    assert(state && state->current_frame == 2);

    const auto& sprite = em.get_component<SpriteComponent>(entity);
    assert(sprite.src_rect.x == 29.0f);
    assert(sprite.src_rect.y == 7.0f);
    assert(sprite.src_rect.w == 10.0f);
    assert(sprite.src_rect.h == 20.0f);
}

} // namespace

int main() {
    test_runtime_state_is_separate_from_authored_animation();
    test_play_clip_and_non_loop_completion();
    test_custom_frames_and_reset();
    test_grid_spacing_margins_and_looping();
    std::cout << "AnimationSystemTests passed\n";
    return 0;
}
