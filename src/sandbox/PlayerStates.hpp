#pragma once

// ---------------------------------------------------------------------------
// PlayerStates.hpp
// Concrete IState<PlayerContext> implementations for the platformer player.
// Each state encapsulates its own enter/update/exit logic and drives
// transitions by calling ctx.fsm->change_state(...) when conditions are met.
// ---------------------------------------------------------------------------

#include "../core/StateMachine.hpp"
#include "../core/Log.hpp"
#include "../core/Input.hpp"
#include "../ecs/Components.hpp"
#include "../physics/PhysicsSystem.hpp"
#include "../particles/ParticlePresets.hpp"
#include "../audio/AudioPresets.hpp"
#include "../renderer/Animation2D.hpp"
#include "../renderer/postprocess/PostProcessPipeline.hpp"
#include "../renderer/postprocess/PostProcessTweens.hpp"
#include <SDL3/SDL.h>
#include <glm/glm.hpp>
#include <memory>

namespace Sandbox {

// ---------------------------------------------------------------------------
// PlayerContext
// Plain data struct passed by reference to every player state.
// Contains all mutable gameplay data the states need — no game logic here.
// ---------------------------------------------------------------------------
struct PlayerContext {
    // --- ECS references ---
    Engine::EntityManager*        entity_manager{nullptr};
    Engine::EntityID              player_id{0};
    Engine::EntityID              elevator_id{0};
    Engine::EntityID              grapple_anchor_id{0};

    // --- FSM self-reference (for state-driven transitions) ---
    Engine::StateMachine<PlayerContext>* fsm{nullptr};

    // --- Physics / movement timers ---
    float coyote_timer{0.0f};
    float jump_buffer_timer{0.0f};
    float launch_cooldown{0.0f};
    float drop_through_timer{0.0f};

    // --- Grapple state ---
    bool is_grappling{false};

    // --- Animation ---
    Engine::Animation2D* anim_idle{nullptr};
    Engine::Animation2D* anim_run{nullptr};
    Engine::Animation2D* anim_jump{nullptr};
    Engine::Animation2D* anim_fall{nullptr};
    Engine::SpriteSheet* knight_spritesheet{nullptr};  // non-null = animated knight loaded
    bool player_facing_right{true};

    // --- External systems ---
    Engine::ParticleSystem2D*    particle_system{nullptr};
    Engine::PostProcessPipeline* post_process_pipeline{nullptr};
};

// ---------------------------------------------------------------------------
// Forward declarations so states can instantiate each other
// ---------------------------------------------------------------------------
class PlayerIdleState;
class PlayerRunState;
class PlayerJumpState;
class PlayerFallState;
class PlayerGrappleState;

// ---------------------------------------------------------------------------
// Helper — shared per-frame input & grounded query
// ---------------------------------------------------------------------------
namespace PlayerStateHelper {

    inline bool is_grounded(PlayerContext& ctx) {
        // Trust the physics system exclusively. The old velocity heuristic
        // (abs(vel.y) < threshold) caused false grounded detection at the apex of a
        // jump and during early descent, enabling infinite double jumps.
        return Engine::PhysicsSystem::is_grounded(*ctx.entity_manager, ctx.player_id);
    }

    inline float get_move_x(float move_speed = 320.0f) {
        float x = 0.0f;
        if (Engine::Input::is_key_pressed(SDL_SCANCODE_A) || Engine::Input::is_key_pressed(SDL_SCANCODE_LEFT))
            x -= move_speed;
        if (Engine::Input::is_key_pressed(SDL_SCANCODE_D) || Engine::Input::is_key_pressed(SDL_SCANCODE_RIGHT))
            x += move_speed;
        return x;
    }

    inline bool jump_pressed() {
        return Engine::Input::is_key_just_pressed(SDL_SCANCODE_SPACE) ||
               Engine::Input::is_key_just_pressed(SDL_SCANCODE_W) ||
               Engine::Input::is_key_just_pressed(SDL_SCANCODE_UP);
    }

    // Apply facing direction and animation frame to the sprite component
    inline void apply_animation(PlayerContext& ctx, Engine::Animation2D* anim, float dt) {
        if (!ctx.knight_spritesheet || !anim) return;
        if (!ctx.entity_manager->has_component<Engine::SpriteComponent>(ctx.player_id)) return;

        auto& sprite = ctx.entity_manager->get_component<Engine::SpriteComponent>(ctx.player_id);
        sprite.flip_x = !ctx.player_facing_right;
        anim->update(dt);
        sprite.use_src_rect = true;
        sprite.src_rect     = anim->get_current_frame_rect();
        sprite.size         = glm::vec2(56.0f, 56.0f);
        sprite.color        = SDL_Color{255, 255, 255, 255};
    }

    inline void update_facing(PlayerContext& ctx, float move_x, float vel_x) {
        if (move_x > 0.0f || vel_x > 15.0f)       ctx.player_facing_right = true;
        else if (move_x < 0.0f || vel_x < -15.0f) ctx.player_facing_right = false;
    }

    // Handle drop-through platform input (S / Down)
    inline void handle_drop_through(PlayerContext& ctx, float dt) {
        if (Engine::Input::is_key_pressed(SDL_SCANCODE_S) || Engine::Input::is_key_pressed(SDL_SCANCODE_DOWN)) {
            ctx.drop_through_timer = 0.25f;
            Engine::PhysicsSystem::set_ignore_one_way(ctx.player_id, true);
        } else if (ctx.drop_through_timer > 0.0f) {
            ctx.drop_through_timer -= dt;
            if (ctx.drop_through_timer <= 0.0f) {
                Engine::PhysicsSystem::set_ignore_one_way(ctx.player_id, false);
            }
        }
    }

    // Emit a jump and transition to jump state
    void do_jump(PlayerContext& ctx);
} // namespace PlayerStateHelper

// ---------------------------------------------------------------------------
// PlayerIdleState
// ---------------------------------------------------------------------------
class PlayerIdleState : public Engine::IState<PlayerContext> {
public:
    std::string get_name() const override { return "Idle"; }

    void on_enter(PlayerContext& ctx) override {
        if (ctx.anim_idle) { ctx.anim_idle->reset(); ctx.anim_idle->play(); }
    }

    void on_update(PlayerContext& ctx, float dt) override;
};

// ---------------------------------------------------------------------------
// PlayerRunState
// ---------------------------------------------------------------------------
class PlayerRunState : public Engine::IState<PlayerContext> {
public:
    std::string get_name() const override { return "Run"; }

    void on_enter(PlayerContext& ctx) override {
        if (ctx.anim_run) { ctx.anim_run->reset(); ctx.anim_run->play(); }
    }

    void on_update(PlayerContext& ctx, float dt) override;
};

// ---------------------------------------------------------------------------
// PlayerJumpState
// ---------------------------------------------------------------------------
class PlayerJumpState : public Engine::IState<PlayerContext> {
public:
    std::string get_name() const override { return "Jump"; }

    void on_enter(PlayerContext& ctx) override {
        // Grace period: Box2D contact resolution lags ~1 physics step, so is_grounded()
        // returns true for 1-2 frames after the jump impulse is applied.
        // We skip the landing check for 100ms to let the body physically leave the ground.
        m_grace_timer = 0.10f;
        if (ctx.anim_jump) { ctx.anim_jump->reset(); ctx.anim_jump->play(); }
    }

    void on_update(PlayerContext& ctx, float dt) override;

private:
    float m_grace_timer{0.0f};
};

// ---------------------------------------------------------------------------
// PlayerFallState
// ---------------------------------------------------------------------------
class PlayerFallState : public Engine::IState<PlayerContext> {
public:
    std::string get_name() const override { return "Fall"; }

    void on_enter(PlayerContext& ctx) override {
        if (ctx.anim_fall) { ctx.anim_fall->reset(); ctx.anim_fall->play(); }
    }

    void on_update(PlayerContext& ctx, float dt) override;
};

// ---------------------------------------------------------------------------
// PlayerGrappleState
// ---------------------------------------------------------------------------
class PlayerGrappleState : public Engine::IState<PlayerContext> {
public:
    std::string get_name() const override { return "Grapple"; }

    void on_enter(PlayerContext& ctx) override {
        ctx.is_grappling = true;
        if (ctx.anim_jump) { ctx.anim_jump->reset(); ctx.anim_jump->play(); }
    }

    void on_exit(PlayerContext& ctx) override {
        ctx.is_grappling = false;
    }

    void on_update(PlayerContext& ctx, float dt) override;
};

// ===========================================================================
// Inline method bodies
// All defined here to avoid a separate .cpp and keep the sandbox self-contained.
// ===========================================================================

namespace PlayerStateHelper {
    inline void do_jump(PlayerContext& ctx) {
        auto& rb = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);
        const auto& tx = ctx.entity_manager->get_component<Engine::TransformComponent>(ctx.player_id);
        rb.velocity.y = 520.0f;
        Engine::Presets::dust_puff(*ctx.particle_system,
            glm::vec2(tx.position.x, tx.position.y - 28.0f), 10);
        Engine::AudioPresets::play_jump(0.6f);
        ctx.jump_buffer_timer = 0.0f;
        ctx.coyote_timer      = 0.0f;
        ENGINE_LOG_INFO("[PLATFORMER DEMO] Jump Applied! Velocity Y set to 520.0");
        ctx.fsm->transition<PlayerJumpState>(ctx);
    }
}

// --- Shared grounded movement logic used by Idle and Run ---
inline void handle_grounded_movement(PlayerContext& ctx, float dt) {
    auto& rb        = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);
    const auto& tx  = ctx.entity_manager->get_component<Engine::TransformComponent>(ctx.player_id);

    const float move_x = PlayerStateHelper::get_move_x();
    rb.velocity.x = move_x;

    PlayerStateHelper::update_facing(ctx, move_x, rb.velocity.x);
    PlayerStateHelper::handle_drop_through(ctx, dt);

    // Coyote time update
    if (PlayerStateHelper::is_grounded(ctx)) {
        ctx.coyote_timer = 0.12f;
    } else {
        ctx.coyote_timer -= dt;
    }

    // Jump buffer
    if (PlayerStateHelper::jump_pressed()) {
        ctx.jump_buffer_timer = 0.12f;
    } else {
        ctx.jump_buffer_timer -= dt;
    }

    // Execute jump
    if (ctx.jump_buffer_timer > 0.0f && ctx.coyote_timer > 0.0f) {
        PlayerStateHelper::do_jump(ctx);
        return;
    }

    // Transition to Fall if no longer grounded (walked off edge)
    if (!PlayerStateHelper::is_grounded(ctx)) {
        ctx.fsm->transition<PlayerFallState>(ctx);
        return;
    }
}

// ---------------------------------------------------------------------------
inline void PlayerIdleState::on_update(PlayerContext& ctx, float dt) {
    handle_grounded_movement(ctx, dt);

    // Transition to Run
    if (ctx.entity_manager->has_component<Engine::RigidBodyComponent>(ctx.player_id)) {
        const auto& rb = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);
        if (std::abs(rb.velocity.x) > 20.0f) {
            ctx.fsm->transition<PlayerRunState>(ctx);
            return;
        }
    }

    PlayerStateHelper::apply_animation(ctx, ctx.anim_idle, dt);
}

// ---------------------------------------------------------------------------
inline void PlayerRunState::on_update(PlayerContext& ctx, float dt) {
    handle_grounded_movement(ctx, dt);

    // Transition to Idle
    if (ctx.entity_manager->has_component<Engine::RigidBodyComponent>(ctx.player_id)) {
        const auto& rb = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);
        if (std::abs(rb.velocity.x) <= 20.0f) {
            ctx.fsm->transition<PlayerIdleState>(ctx);
            return;
        }
    }

    PlayerStateHelper::apply_animation(ctx, ctx.anim_run, dt);
}

// ---------------------------------------------------------------------------
inline void PlayerJumpState::on_update(PlayerContext& ctx, float dt) {
    if (!ctx.entity_manager->has_component<Engine::RigidBodyComponent>(ctx.player_id)) return;

    auto& rb = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);

    // Decay grace timer — skip landing/fall checks while it's active
    m_grace_timer -= dt;

    // Air control
    const float move_x = PlayerStateHelper::get_move_x();
    rb.velocity.x = move_x;
    PlayerStateHelper::update_facing(ctx, move_x, rb.velocity.x);
    PlayerStateHelper::handle_drop_through(ctx, dt);

    if (m_grace_timer <= 0.0f) {
        // Transition to Fall when apex reached
        if (rb.velocity.y < 0.0f) {
            ctx.fsm->transition<PlayerFallState>(ctx);
            return;
        }

        // Land (e.g. bumped head on ceiling and fell back)
        if (PlayerStateHelper::is_grounded(ctx)) {
            if (std::abs(rb.velocity.x) > 20.0f)
                ctx.fsm->transition<PlayerRunState>(ctx);
            else
                ctx.fsm->transition<PlayerIdleState>(ctx);
            return;
        }
    }

    PlayerStateHelper::apply_animation(ctx, ctx.anim_jump, dt);
}

// ---------------------------------------------------------------------------
inline void PlayerFallState::on_update(PlayerContext& ctx, float dt) {
    if (!ctx.entity_manager->has_component<Engine::RigidBodyComponent>(ctx.player_id)) return;

    auto& rb = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);

    // Air control
    const float move_x = PlayerStateHelper::get_move_x();
    rb.velocity.x = move_x;
    PlayerStateHelper::update_facing(ctx, move_x, rb.velocity.x);
    PlayerStateHelper::handle_drop_through(ctx, dt);

    // Coyote timer decays while falling
    ctx.coyote_timer -= dt;

    // Jump buffer input while falling
    if (PlayerStateHelper::jump_pressed()) {
        ctx.jump_buffer_timer = 0.12f;
    } else {
        ctx.jump_buffer_timer -= dt;
    }

    // Coyote jump
    if (ctx.jump_buffer_timer > 0.0f && ctx.coyote_timer > 0.0f) {
        PlayerStateHelper::do_jump(ctx);
        return;
    }

    // Land — PhysicsSystem::is_grounded() is the authoritative check
    if (PlayerStateHelper::is_grounded(ctx)) {
        if (std::abs(rb.velocity.x) > 20.0f)
            ctx.fsm->transition<PlayerRunState>(ctx);
        else
            ctx.fsm->transition<PlayerIdleState>(ctx);
        return;
    }

    PlayerStateHelper::apply_animation(ctx, ctx.anim_fall, dt);
}

// ---------------------------------------------------------------------------
inline void PlayerGrappleState::on_update(PlayerContext& ctx, float dt) {
    if (!ctx.entity_manager->has_component<Engine::RigidBodyComponent>(ctx.player_id)) return;

    auto& rb = ctx.entity_manager->get_component<Engine::RigidBodyComponent>(ctx.player_id);

    // Pendulum pumping with A/D
    const float move_x = PlayerStateHelper::get_move_x();
    if (move_x != 0.0f) {
        rb.velocity.x += (move_x > 0.0f ? 600.0f : -600.0f) * dt;
        rb.velocity.x = std::clamp(rb.velocity.x, -550.0f, 550.0f);
    }
    PlayerStateHelper::update_facing(ctx, move_x, rb.velocity.x);

    // Jump from grapple = catapult launch
    if (PlayerStateHelper::jump_pressed()) {
        const auto& tx = ctx.entity_manager->get_component<Engine::TransformComponent>(ctx.player_id);
        rb.velocity.y = std::max(rb.velocity.y + 260.0f, 480.0f);
        Engine::Presets::dust_puff(*ctx.particle_system,
            glm::vec2(tx.position.x, tx.position.y - 28.0f), 12);
        ENGINE_LOG_INFO("[GRAPPLE JUMP] Grapple Released with Catapult Momentum Boost!");
        // Signal the scene to detach the grapple (scene will call detach + change state)
        ctx.is_grappling = false;
        ctx.fsm->transition<PlayerJumpState>(ctx);
        return;
    }

    PlayerStateHelper::apply_animation(ctx, ctx.anim_jump, dt);
}

} // namespace Sandbox
