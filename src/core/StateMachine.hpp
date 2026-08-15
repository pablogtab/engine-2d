#pragma once

#include "Log.hpp"
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cassert>

namespace Engine {

// ---------------------------------------------------------------------------
// IState<TContext>
// Abstract contract for any state in a StateMachine.
// Supports standard lifecycle (enter/update/exit) plus pushdown lifecycle
// (pause/resume) for temporary sub-states (e.g. Stunned, Menu, Cutscene).
// ---------------------------------------------------------------------------
template<typename TContext>
class IState {
public:
    virtual ~IState() = default;

    // Called once when entering this state.
    virtual void on_enter(TContext& ctx) {}

    // Called every frame while this state is actively running.
    virtual void on_update(TContext& ctx, float dt) = 0;

    // Called once when exiting this state (either destroyed or replaced).
    virtual void on_exit(TContext& ctx) {}

    // Called when a new state is pushed on top of this state in the stack.
    virtual void on_pause(TContext& ctx) {}

    // Called when the state on top is popped and this state becomes active again.
    virtual void on_resume(TContext& ctx) {}

    // Human-readable name used for logging and state queries.
    virtual std::string get_name() const = 0;
};

// ---------------------------------------------------------------------------
// StateMachine<TContext>
// General-purpose strongly-typed FSM & Pushdown Automaton.
// ---------------------------------------------------------------------------
template<typename TContext>
class StateMachine {
public:
    StateMachine() = default;

    // Stores a reference to ctx so it doesn't need to be passed on every call.
    void bind_context(TContext& ctx) {
        m_ctx = &ctx;
    }

    // ---------------------------------------------------------------------------
    // Preferred API (bound context)
    // ---------------------------------------------------------------------------

    // Standard transition: replaces the active state (and clears any paused stack).
    template<typename TState, typename... Args>
    void transition(Args&&... args) {
        assert(m_ctx && "[FSM] bind_context() must be called before transition()");
        _change_state(std::make_unique<TState>(std::forward<Args>(args)...), *m_ctx);
    }

    // Push state: pauses the current state and pushes a new one on top of the stack.
    template<typename TState, typename... Args>
    void push(Args&&... args) {
        assert(m_ctx && "[FSM] bind_context() must be called before push()");
        _push_state(std::make_unique<TState>(std::forward<Args>(args)...), *m_ctx);
    }

    // Pop state: exits the current state and resumes the previous one from the stack.
    bool pop() {
        assert(m_ctx && "[FSM] bind_context() must be called before pop()");
        return _pop_state(*m_ctx);
    }

    // Per-frame update: advances state time and calls on_update.
    void update(float dt) {
        assert(m_ctx && "[FSM] bind_context() must be called before update()");
        _update(*m_ctx, dt);
    }

    // ---------------------------------------------------------------------------
    // Explicit ctx overloads (for unbound or external callers)
    // ---------------------------------------------------------------------------

    void change_state(std::unique_ptr<IState<TContext>> new_state, TContext& ctx) {
        _change_state(std::move(new_state), ctx);
    }

    template<typename TState, typename... Args>
    void transition(TContext& ctx, Args&&... args) {
        _change_state(std::make_unique<TState>(std::forward<Args>(args)...), ctx);
    }

    template<typename TState, typename... Args>
    void push(TContext& ctx, Args&&... args) {
        _push_state(std::make_unique<TState>(std::forward<Args>(args)...), ctx);
    }

    void push_state(std::unique_ptr<IState<TContext>> new_state, TContext& ctx) {
        _push_state(std::move(new_state), ctx);
    }

    bool pop_state(TContext& ctx) {
        return _pop_state(ctx);
    }

    void update(TContext& ctx, float dt) {
        _update(ctx, dt);
    }

    // ---------------------------------------------------------------------------
    // Queries & State Lifetime
    // ---------------------------------------------------------------------------

    // Returns the total time in seconds the current state has been active.
    [[nodiscard]] float get_state_time() const {
        return m_state_time;
    }

    [[nodiscard]] std::string get_current_state_name() const {
        return m_current_state ? m_current_state->get_name() : "None";
    }

    [[nodiscard]] bool is_in_state(std::string_view name) const {
        return m_current_state && m_current_state->get_name() == name;
    }

    [[nodiscard]] IState<TContext>* get_current_state() const {
        return m_current_state.get();
    }

    [[nodiscard]] size_t get_stack_depth() const {
        return m_state_stack.size();
    }

    [[nodiscard]] bool has_stacked_states() const {
        return !m_state_stack.empty();
    }

private:
    void _change_state(std::unique_ptr<IState<TContext>> new_state, TContext& ctx) {
        if (!new_state) return;

        const std::string old_name = m_current_state ? m_current_state->get_name() : "None";
        const std::string new_name = new_state->get_name();

        // Clear any suspended states on a full transition
        while (!m_state_stack.empty()) {
            m_state_stack.back()->on_exit(ctx);
            m_state_stack.pop_back();
        }

        if (m_current_state) {
            m_current_state->on_exit(ctx);
        }

        m_current_state = std::move(new_state);
        m_state_time = 0.0f;
        ENGINE_LOG_INFO("[FSM] {} -> {}", old_name, new_name);
        m_current_state->on_enter(ctx);
    }

    void _push_state(std::unique_ptr<IState<TContext>> new_state, TContext& ctx) {
        if (!new_state) return;

        const std::string old_name = m_current_state ? m_current_state->get_name() : "None";
        const std::string new_name = new_state->get_name();

        if (m_current_state) {
            m_current_state->on_pause(ctx);
            m_state_stack.push_back(std::move(m_current_state));
        }

        m_current_state = std::move(new_state);
        m_state_time = 0.0f;
        ENGINE_LOG_INFO("[FSM PUSH] {} -> {} (Stack Depth: {})", old_name, new_name, m_state_stack.size());
        m_current_state->on_enter(ctx);
    }

    bool _pop_state(TContext& ctx) {
        if (m_state_stack.empty()) {
            ENGINE_LOG_WARN("[FSM POP] Attempted to pop state with empty stack.");
            return false;
        }

        const std::string exiting_name = m_current_state ? m_current_state->get_name() : "None";

        if (m_current_state) {
            m_current_state->on_exit(ctx);
        }

        m_current_state = std::move(m_state_stack.back());
        m_state_stack.pop_back();
        m_state_time = 0.0f;

        const std::string resumed_name = m_current_state ? m_current_state->get_name() : "None";
        ENGINE_LOG_INFO("[FSM POP] {} -> Resumed {} (Stack Depth: {})", exiting_name, resumed_name, m_state_stack.size());
        m_current_state->on_resume(ctx);
        return true;
    }

    void _update(TContext& ctx, float dt) {
        m_state_time += dt;
        if (m_current_state) {
            m_current_state->on_update(ctx, dt);
        }
    }

    std::unique_ptr<IState<TContext>> m_current_state;
    std::vector<std::unique_ptr<IState<TContext>>> m_state_stack;
    float m_state_time{0.0f};
    TContext* m_ctx{nullptr};
};

} // namespace Engine
