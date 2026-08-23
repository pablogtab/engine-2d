#include "AsyncScheduler.hpp"
#include <algorithm>

namespace EngineScript {

void AsyncScheduler::DelayAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    if (scheduler) {
        ScheduledAction act;
        act.id = scheduler->next_action_id();
        act.scope_id = scope_id;
        act.time_remaining = seconds;
        act.is_real_time = is_real_time;
        act.coroutine_handle = h;
        scheduler->m_actions.push_back(std::move(act));
    }
}

void AsyncScheduler::NextFrameAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    if (scheduler) {
        ScheduledAction act;
        act.id = scheduler->next_action_id();
        act.scope_id = scope_id;
        act.is_next_frame = true;
        act.coroutine_handle = h;
        scheduler->m_actions.push_back(std::move(act));
    }
}

uint64_t AsyncScheduler::schedule_delay(float seconds, std::function<void()> callback, uint32_t scope_id) {
    ScheduledAction act;
    act.id = next_action_id();
    act.scope_id = scope_id;
    act.time_remaining = seconds;
    act.is_real_time = false;
    act.callback = std::move(callback);
    m_actions.push_back(std::move(act));
    return act.id;
}

uint64_t AsyncScheduler::schedule_real_delay(float seconds, std::function<void()> callback, uint32_t scope_id) {
    ScheduledAction act;
    act.id = next_action_id();
    act.scope_id = scope_id;
    act.time_remaining = seconds;
    act.is_real_time = true;
    act.callback = std::move(callback);
    m_actions.push_back(std::move(act));
    return act.id;
}

uint64_t AsyncScheduler::schedule_next_frame(std::function<void()> callback, uint32_t scope_id) {
    ScheduledAction act;
    act.id = next_action_id();
    act.scope_id = scope_id;
    act.is_next_frame = true;
    act.callback = std::move(callback);
    m_actions.push_back(std::move(act));
    return act.id;
}

uint64_t AsyncScheduler::schedule_fixed_update(std::function<void()> callback, uint32_t scope_id) {
    ScheduledAction act;
    act.id = next_action_id();
    act.scope_id = scope_id;
    act.is_fixed_update = true;
    act.callback = std::move(callback);
    m_actions.push_back(std::move(act));
    return act.id;
}

void AsyncScheduler::cancel_scope(uint32_t scope_id) {
    for (auto& act : m_actions) {
        if (act.scope_id == scope_id) {
            act.is_cancelled = true;
        }
    }
}

void AsyncScheduler::cancel_all() {
    for (auto& act : m_actions) {
        act.is_cancelled = true;
    }
    m_actions.clear();
}

void AsyncScheduler::reset() {
    m_actions.clear();
    m_id_counter = 0;
}

size_t AsyncScheduler::pending_tasks_count() const {
    size_t count = 0;
    for (const auto& act : m_actions) {
        if (!act.is_cancelled) ++count;
    }
    return count;
}

void AsyncScheduler::step(float game_delta_time, float real_delta_time) {
    std::vector<ScheduledAction> to_run;

    for (auto& act : m_actions) {
        if (act.is_cancelled) continue;

        if (act.is_next_frame) {
            to_run.push_back(act);
            act.is_cancelled = true;
        } else if (!act.is_fixed_update) {
            float dt = act.is_real_time ? real_delta_time : game_delta_time;
            act.time_remaining -= dt;
            if (act.time_remaining <= 0.0f) {
                to_run.push_back(act);
                act.is_cancelled = true;
            }
        }
    }

    // Remove finished / cancelled actions
    m_actions.erase(
        std::remove_if(m_actions.begin(), m_actions.end(), [](const ScheduledAction& a) { return a.is_cancelled; }),
        m_actions.end()
    );

    // Execute triggered callbacks and resume coroutines
    for (auto& act : to_run) {
        if (act.callback) {
            act.callback();
        }
        if (act.coroutine_handle && !act.coroutine_handle.done()) {
            act.coroutine_handle.resume();
        }
    }
}

void AsyncScheduler::step_fixed_update() {
    std::vector<ScheduledAction> to_run;

    for (auto& act : m_actions) {
        if (!act.is_cancelled && act.is_fixed_update) {
            to_run.push_back(act);
            act.is_cancelled = true;
        }
    }

    m_actions.erase(
        std::remove_if(m_actions.begin(), m_actions.end(), [](const ScheduledAction& a) { return a.is_cancelled; }),
        m_actions.end()
    );

    for (auto& act : to_run) {
        if (act.callback) {
            act.callback();
        }
        if (act.coroutine_handle && !act.coroutine_handle.done()) {
            act.coroutine_handle.resume();
        }
    }
}

} // namespace EngineScript
