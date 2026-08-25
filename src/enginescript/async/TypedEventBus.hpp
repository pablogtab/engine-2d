#pragma once

#include "Task.hpp"

#include <coroutine>
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace EngineScript {

class TypedEventBus {
public:
    template <typename T>
    void emit(T payload) {
        std::function<void(std::shared_ptr<const void>)> resume;
        auto value = std::make_shared<const T>(std::move(payload));
        {
            std::scoped_lock lock(m_mutex);
            const std::type_index type = typeid(T);
            const auto waiter = std::find_if(m_waiters.begin(), m_waiters.end(),
                [type](const Waiter& candidate) { return candidate.type == type; });
            if (waiter != m_waiters.end()) {
                resume = std::move(waiter->resume);
                m_waiters.erase(waiter);
            } else {
                m_queued[type].push_back(value);
            }
        }
        if (resume) resume(std::move(value));
    }

    template <typename T>
    struct NextAwaiter {
        struct State {
            std::optional<T> value;
            std::coroutine_handle<> continuation;
            std::weak_ptr<detail::CoroutineControlBlock> control;
            std::uint64_t waiter_id{0};
        };

        TypedEventBus* bus{nullptr};
        std::uint32_t scope_id{0};
        std::shared_ptr<State> state = std::make_shared<State>();

        bool await_ready() {
            return bus && bus->try_pop<T>(state->value);
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> continuation) {
            static_assert(std::is_base_of_v<detail::TaskPromiseBase, Promise>);
            state->continuation = continuation;
            state->control = continuation.promise().control;
            if (!bus) return;
            const std::weak_ptr<State> weak_state = state;
            state->waiter_id = bus->register_waiter(typeid(T), scope_id,
                [weak_state](std::shared_ptr<const void> raw_value) {
                    const auto locked = weak_state.lock();
                    if (!locked) return;
                    const auto control = locked->control.lock();
                    if (!control || control->cancelled) return;
                    locked->value = *std::static_pointer_cast<const T>(std::move(raw_value));
                    if (locked->continuation && !locked->continuation.done()) locked->continuation.resume();
                });
        }

        T await_resume() { return state->value.value_or(T{}); }

        ~NextAwaiter() {
            if (bus && state && state->waiter_id != 0) bus->unregister_waiter(state->waiter_id);
        }
    };

    template <typename T>
    NextAwaiter<T> next(std::uint32_t scope_id = 0) {
        return NextAwaiter<T>{this, scope_id};
    }

    void cancel_scope(std::uint32_t scope_id) {
        std::scoped_lock lock(m_mutex);
        std::erase_if(m_waiters, [scope_id](const Waiter& waiter) { return waiter.scope_id == scope_id; });
    }

    void clear() {
        std::scoped_lock lock(m_mutex);
        m_waiters.clear();
        m_queued.clear();
    }

private:
    struct Waiter {
        std::uint64_t id{0};
        std::type_index type{typeid(void)};
        std::uint32_t scope_id{0};
        std::function<void(std::shared_ptr<const void>)> resume;
    };

    template <typename T>
    bool try_pop(std::optional<T>& destination) {
        std::scoped_lock lock(m_mutex);
        const auto found = m_queued.find(typeid(T));
        if (found == m_queued.end() || found->second.empty()) return false;
        destination = *std::static_pointer_cast<const T>(found->second.front());
        found->second.pop_front();
        if (found->second.empty()) m_queued.erase(found);
        return true;
    }

    std::uint64_t register_waiter(
        std::type_index type,
        std::uint32_t scope_id,
        std::function<void(std::shared_ptr<const void>)> resume
    ) {
        std::scoped_lock lock(m_mutex);
        const std::uint64_t id = ++m_next_waiter_id;
        m_waiters.push_back({id, type, scope_id, std::move(resume)});
        return id;
    }

    void unregister_waiter(std::uint64_t id) {
        std::scoped_lock lock(m_mutex);
        std::erase_if(m_waiters, [id](const Waiter& waiter) { return waiter.id == id; });
    }

    std::mutex m_mutex;
    std::uint64_t m_next_waiter_id{0};
    std::vector<Waiter> m_waiters;
    std::unordered_map<std::type_index, std::deque<std::shared_ptr<const void>>> m_queued;
};

class Events {
public:
    Events() = default;
    Events(TypedEventBus* bus, std::uint32_t scope_id) : m_bus(bus), m_scope_id(scope_id) {}

    template <typename T>
    bool emit(const T& payload) {
        if (!m_bus) return false;
        m_bus->emit<T>(payload);
        return true;
    }

    template <typename T>
    auto next() {
        return m_bus ? m_bus->next<T>(m_scope_id) : typename TypedEventBus::NextAwaiter<T>{};
    }

private:
    TypedEventBus* m_bus{nullptr};
    std::uint32_t m_scope_id{0};
};

} // namespace EngineScript
