#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>
#include <optional>
#include <functional>
#include <memory>
#include <utility>

namespace EngineScript {

enum class TaskState {
    Pending,
    Completed,
    Failed,
    Cancelled
};

template <typename T = void>
class Task;

namespace detail {

struct CoroutineControlBlock {
    bool cancelled = false;
    TaskState* state = nullptr;
};

struct TaskPromiseBase {
    TaskState state = TaskState::Pending;
    std::exception_ptr exception;
    std::coroutine_handle<> continuation;
    uint32_t task_id = 0;
    uint32_t scope_id = 0;
    std::shared_ptr<CoroutineControlBlock> control = std::make_shared<CoroutineControlBlock>();

    std::suspend_never initial_suspend() noexcept { return {}; }
    
    struct FinalAwaiter {
        bool await_ready() noexcept { return false; }
        template <typename PromiseType>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<PromiseType> h) noexcept {
            if (h.promise().continuation) {
                return h.promise().continuation;
            }
            return std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    void unhandled_exception() noexcept {
        exception = std::current_exception();
        state = TaskState::Failed;
    }
};

template <typename T>
struct TaskPromise : public TaskPromiseBase {
    std::optional<T> result;

    Task<T> get_return_object();

    void return_value(T val) {
        result = std::move(val);
        state = TaskState::Completed;
    }
};

template <>
struct TaskPromise<void> : public TaskPromiseBase {
    Task<void> get_return_object();

    void return_void() {
        state = TaskState::Completed;
    }
};

} // namespace detail

template <typename T>
class Task {
public:
    using promise_type = detail::TaskPromise<T>;

    Task() = default;
    explicit Task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}

    Task(Task&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            release();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    ~Task() {
        release();
    }

    bool is_valid() const { return m_handle != nullptr; }
    TaskState state() const { return m_handle ? m_handle.promise().state : TaskState::Pending; }
    bool is_done() const { return state() == TaskState::Completed || state() == TaskState::Failed || state() == TaskState::Cancelled; }

    T get_result() const {
        if (m_handle && m_handle.promise().result) {
            return *m_handle.promise().result;
        }
        return T{};
    }

    void cancel() {
        if (m_handle && m_handle.promise().state == TaskState::Pending) {
            m_handle.promise().state = TaskState::Cancelled;
            m_handle.promise().control->cancelled = true;
        }
    }

    bool await_ready() const noexcept {
        return is_done();
    }

    void await_suspend(std::coroutine_handle<> cont) noexcept {
        if (m_handle) {
            m_handle.promise().continuation = cont;
        }
    }

    T await_resume() {
        if (m_handle && m_handle.promise().exception) {
            std::rethrow_exception(m_handle.promise().exception);
        }
        return get_result();
    }

private:
    void release() {
        if (!m_handle) return;
        auto control = m_handle.promise().control;
        control->cancelled = true;
        if (m_handle.promise().state == TaskState::Pending) {
            m_handle.promise().state = TaskState::Cancelled;
        }
        control->state = nullptr;
        m_handle.destroy();
        m_handle = nullptr;
    }

    std::coroutine_handle<promise_type> m_handle = nullptr;
};

template <>
class Task<void> {
public:
    using promise_type = detail::TaskPromise<void>;

    Task() = default;
    explicit Task(std::coroutine_handle<promise_type> handle) : m_handle(handle) {}

    Task(Task&& other) noexcept : m_handle(other.m_handle) {
        other.m_handle = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            release();
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    ~Task() {
        release();
    }

    bool is_valid() const { return m_handle != nullptr; }
    TaskState state() const { return m_handle ? m_handle.promise().state : TaskState::Pending; }
    bool is_done() const { return state() == TaskState::Completed || state() == TaskState::Failed || state() == TaskState::Cancelled; }

    void cancel() {
        if (m_handle && m_handle.promise().state == TaskState::Pending) {
            m_handle.promise().state = TaskState::Cancelled;
            m_handle.promise().control->cancelled = true;
        }
    }

    bool await_ready() const noexcept {
        return is_done();
    }

    void await_suspend(std::coroutine_handle<> cont) noexcept {
        if (m_handle) {
            m_handle.promise().continuation = cont;
        }
    }

    void await_resume() {
        if (m_handle && m_handle.promise().exception) {
            std::rethrow_exception(m_handle.promise().exception);
        }
    }

private:
    void release() {
        if (!m_handle) return;
        auto control = m_handle.promise().control;
        control->cancelled = true;
        if (m_handle.promise().state == TaskState::Pending) {
            m_handle.promise().state = TaskState::Cancelled;
        }
        control->state = nullptr;
        m_handle.destroy();
        m_handle = nullptr;
    }

    std::coroutine_handle<promise_type> m_handle = nullptr;
};

namespace detail {
template <typename T>
inline Task<T> TaskPromise<T>::get_return_object() {
    control->state = &state;
    return Task<T>(std::coroutine_handle<TaskPromise<T>>::from_promise(*this));
}

inline Task<void> TaskPromise<void>::get_return_object() {
    control->state = &state;
    return Task<void>(std::coroutine_handle<TaskPromise<void>>::from_promise(*this));
}
} // namespace detail

} // namespace EngineScript
