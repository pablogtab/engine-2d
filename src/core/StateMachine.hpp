#pragma once

#include "Log.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <cassert>

namespace Engine {

// ---------------------------------------------------------------------------
// IState<TContext>
// ---------------------------------------------------------------------------
template<typename TContext>
class IState {
public:
    virtual ~IState() = default;

    virtual void on_enter(TContext& ctx) {}
    virtual void on_update(TContext& ctx, float dt) = 0;
    virtual void on_exit(TContext& ctx) {}
    virtual std::string get_name() const = 0;
};

// ---------------------------------------------------------------------------
// StateMachine<TContext>
// Owns exactly one active IState<TContext> at a time.
//
// Typical usage:
//   fsm.bind_context(ctx);              // once at init
//   fsm.transition<PlayerIdleState>();  // no ctx needed
//   fsm.update(dt);                     // no ctx needed
// ---------------------------------------------------------------------------
template<typename TContext>
class StateMachine {
public:
    StateMachine() = default;

    // Stores a reference to ctx so it doesn't need to be passed on every call.
    // Must be called once before using the no-ctx overloads.
    void bind_context(TContext& ctx) {
        m_ctx = &ctx;
    }

    // ---------------------------------------------------------------------------
    // Preferred API (after bind_context)
    // ---------------------------------------------------------------------------

    // Construct TState in-place and transition to it — no ctx needed.
    // Usage: ctx.fsm->transition<PlayerRunState>();
    template<typename TState, typename... Args>
    void transition(Args&&... args) {
        assert(m_ctx && "[FSM] bind_context() must be called before transition()");
        _change_state(std::make_unique<TState>(std::forward<Args>(args)...), *m_ctx);
    }

    // Per-frame update — no ctx needed.
    void update(float dt) {
        assert(m_ctx && "[FSM] bind_context() must be called before update()");
        if (m_current_state) {
            m_current_state->on_update(*m_ctx, dt);
        }
    }

    // ---------------------------------------------------------------------------
    // Explicit ctx overloads — kept for external callers (e.g. scene on_attach)
    // ---------------------------------------------------------------------------

    void change_state(std::unique_ptr<IState<TContext>> new_state, TContext& ctx) {
        _change_state(std::move(new_state), ctx);
    }

    template<typename TState, typename... Args>
    void transition(TContext& ctx, Args&&... args) {
        _change_state(std::make_unique<TState>(std::forward<Args>(args)...), ctx);
    }

    void update(TContext& ctx, float dt) {
        if (m_current_state) {
            m_current_state->on_update(ctx, dt);
        }
    }

    // ---------------------------------------------------------------------------
    // Queries
    // ---------------------------------------------------------------------------

    [[nodiscard]] std::string get_current_state_name() const {
        return m_current_state ? m_current_state->get_name() : "None";
    }

    [[nodiscard]] bool is_in_state(std::string_view name) const {
        return m_current_state && m_current_state->get_name() == name;
    }

    [[nodiscard]] IState<TContext>* get_current_state() const {
        return m_current_state.get();
    }

private:
    void _change_state(std::unique_ptr<IState<TContext>> new_state, TContext& ctx) {
        if (!new_state) return;
        const std::string old_name = m_current_state ? m_current_state->get_name() : "None";
        const std::string new_name = new_state->get_name();
        if (m_current_state) m_current_state->on_exit(ctx);
        m_current_state = std::move(new_state);
        ENGINE_LOG_INFO("[FSM] {} -> {}", old_name, new_name);
        m_current_state->on_enter(ctx);
    }

    std::unique_ptr<IState<TContext>> m_current_state;
    TContext* m_ctx{nullptr};
};

} // namespace Engine
