#pragma once

#include "Task.hpp"
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>
#include <type_traits>

namespace EngineScript {

struct ScheduledAction {
    uint64_t id = 0;
    uint32_t scope_id = 0;
    float time_remaining = 0.0f;
    bool is_real_time = false;
    bool is_next_frame = false;
    bool is_fixed_update = false;
    bool is_cancelled = false;
    std::function<void()> callback;
    std::coroutine_handle<> coroutine_handle;
    std::weak_ptr<detail::CoroutineControlBlock> coroutine_control;
};

class AsyncScheduler {
public:
    AsyncScheduler() = default;

    uint64_t schedule_delay(float seconds, std::function<void()> callback, uint32_t scope_id = 0);
    uint64_t schedule_real_delay(float seconds, std::function<void()> callback, uint32_t scope_id = 0);
    uint64_t schedule_next_frame(std::function<void()> callback, uint32_t scope_id = 0);
    uint64_t schedule_fixed_update(std::function<void()> callback, uint32_t scope_id = 0);

    uint64_t post_next_frame(std::function<void()> callback, uint32_t scope_id = 0) {
        ScheduledAction action;
        action.id = next_action_id();
        action.scope_id = scope_id;
        action.is_next_frame = true;
        action.callback = std::move(callback);
        const uint64_t id = action.id;
        enqueue_action(std::move(action));
        return id;
    }

    void cancel_scope(uint32_t scope_id);
    void cancel_all();

    void step(float game_delta_time, float real_delta_time);
    void step_fixed_update();

    size_t pending_tasks_count() const;
    void reset();

    // Awaitable helper struct for coroutines
    struct DelayAwaiter {
        AsyncScheduler* scheduler;
        float seconds;
        uint32_t scope_id;
        bool is_real_time = false;

        bool await_ready() const noexcept { return seconds <= 0.0f; }
        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) noexcept {
            static_assert(std::is_base_of_v<detail::TaskPromiseBase, Promise>);
            if (!scheduler) return;
            ScheduledAction action;
            action.id = scheduler->next_action_id();
            action.scope_id = scope_id;
            action.time_remaining = seconds;
            action.is_real_time = is_real_time;
            action.coroutine_handle = h;
            action.coroutine_control = h.promise().control;
            scheduler->enqueue_action(std::move(action));
        }
        void await_resume() const noexcept {}
    };

    struct NextFrameAwaiter {
        AsyncScheduler* scheduler;
        uint32_t scope_id;

        bool await_ready() const noexcept { return false; }
        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) noexcept {
            static_assert(std::is_base_of_v<detail::TaskPromiseBase, Promise>);
            if (!scheduler) return;
            ScheduledAction action;
            action.id = scheduler->next_action_id();
            action.scope_id = scope_id;
            action.is_next_frame = true;
            action.coroutine_handle = h;
            action.coroutine_control = h.promise().control;
            scheduler->enqueue_action(std::move(action));
        }
        void await_resume() const noexcept {}
    };

    struct FixedUpdateAwaiter {
        AsyncScheduler* scheduler;
        uint32_t scope_id;

        bool await_ready() const noexcept { return false; }
        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) noexcept {
            static_assert(std::is_base_of_v<detail::TaskPromiseBase, Promise>);
            if (!scheduler) return;
            ScheduledAction action;
            action.id = scheduler->next_action_id();
            action.scope_id = scope_id;
            action.is_fixed_update = true;
            action.coroutine_handle = h;
            action.coroutine_control = h.promise().control;
            scheduler->enqueue_action(std::move(action));
        }
        void await_resume() const noexcept {}
    };

    DelayAwaiter delay(float seconds, uint32_t scope_id = 0) {
        return DelayAwaiter{this, seconds, scope_id, false};
    }

    DelayAwaiter real_delay(float seconds, uint32_t scope_id = 0) {
        return DelayAwaiter{this, seconds, scope_id, true};
    }

    NextFrameAwaiter next_frame(uint32_t scope_id = 0) {
        return NextFrameAwaiter{this, scope_id};
    }

    FixedUpdateAwaiter fixed_update(uint32_t scope_id = 0) {
        return FixedUpdateAwaiter{this, scope_id};
    }

private:
    uint64_t next_action_id() { return ++m_id_counter; }
    void enqueue_action(ScheduledAction action) {
        std::scoped_lock lock(m_actions_mutex);
        m_actions.push_back(std::move(action));
    }

    std::atomic_uint64_t m_id_counter{0};
    mutable std::mutex m_actions_mutex;
    std::vector<ScheduledAction> m_actions;
};

} // namespace EngineScript
